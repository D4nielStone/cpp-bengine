#pragma once

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <set>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace editor::ui::project_files {
namespace fs = std::filesystem;
struct document { fs::path path; std::string contents; };
// Internal test seam: tests terminate a subprocess at actual transaction boundaries.
using checkpoint = std::function<void(const char*, std::size_t)>;
inline fs::path journal_path(const fs::path& anchor) {
    return fs::path(anchor.string() + ".transaction");
}
inline fs::path temporary_path(const fs::path& path) {
    return fs::path(path.string() + ".tmp");
}
inline bool regular_or_absent(const fs::path& path) {
    const auto status = fs::symlink_status(path);
    return !fs::exists(status) || (fs::is_regular_file(status) && fs::hard_link_count(path) == 1);
}
inline std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read '" + path.string() + "'.");
    std::string result{std::istreambuf_iterator<char>(input), {}};
    if (input.bad()) throw std::runtime_error("Incomplete read of '" + path.string() + "'.");
    return result;
}
inline void stage(const document& doc) {
    const auto temp = temporary_path(doc.path);
    if (!regular_or_absent(doc.path) || !regular_or_absent(temp))
        throw std::runtime_error("Unsafe or non-file save destination: " + doc.path.string());
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("Could not open temporary file: " + temp.string());
    output.write(doc.contents.data(), static_cast<std::streamsize>(doc.contents.size()));
    output.flush();
    if (!output) throw std::runtime_error("Could not finish writing: " + temp.string());
    output.close();
    if (output.fail()) throw std::runtime_error("Could not close: " + temp.string());
}
inline void replace(const fs::path& path) {
    if (!regular_or_absent(path))
        throw std::runtime_error("Unsafe or non-file replacement destination: " + path.string());
#ifdef _WIN32
    if (!MoveFileExW(temporary_path(path).c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
        throw std::runtime_error("Could not replace file: " + path.string());
#else
    fs::rename(temporary_path(path), path);
#endif
}
inline void remove_journal(const fs::path& path) {
    if (!fs::remove(path)) throw std::runtime_error("Could not remove transaction: " + path.string());
}
inline void clean_temporaries(const std::vector<document>& documents) noexcept {
    for (const auto& doc : documents) {
        std::error_code ignored;
        fs::remove(temporary_path(doc.path), ignored);
    }
}
inline std::string encode(const rapidjson::Document& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}

// The journal contains complete preimages, retained until every replacement is
// finished. Recovery is idempotent, including interruption during recovery.
// This protocol covers process termination, not power-loss durability (no fsync).
inline bool recover(const fs::path& input_anchor, std::string& error,
                    const checkpoint& observe = {}) {
    try {
        const auto anchor = fs::absolute(input_anchor).lexically_normal();
        const auto journal = journal_path(anchor);
        if (!fs::exists(fs::symlink_status(journal))) return true;
        if (!regular_or_absent(journal)) throw std::runtime_error("Invalid transaction file.");
        rapidjson::Document value;
        const auto contents = read(journal);
        value.Parse(contents.data(), contents.size());
        if (value.HasParseError() || !value.IsObject() || !value.HasMember("version") ||
            !value["version"].IsUint() || value["version"].GetUint() != 1 ||
            !value.HasMember("files") || !value["files"].IsArray() ||
            value["files"].Empty() || value["files"].Size() > 2)
            throw std::runtime_error("Invalid transaction record; files were preserved.");
        const auto& files = value["files"];
        std::vector<document> originals;
        std::vector<bool> existed;
        std::set<fs::path> reserved{journal, temporary_path(journal)};
        // Validate the entire record before touching any target.
        for (const auto& item : files.GetArray()) {
            if (!item.IsObject() || !item.HasMember("path") || !item["path"].IsString() ||
                !item.HasMember("before") || (!item["before"].IsNull() && !item["before"].IsString()) ||
                !item.HasMember("after") || !item["after"].IsString())
                throw std::runtime_error("Invalid transaction entry.");
            const fs::path target(std::string(item["path"].GetString(), item["path"].GetStringLength()));
            if (!target.is_absolute() || target != target.lexically_normal() ||
                !regular_or_absent(target) || !reserved.insert(target).second ||
                !reserved.insert(temporary_path(target)).second)
                throw std::runtime_error("Invalid transaction destination.");
            existed.push_back(!item["before"].IsNull());
            originals.push_back({target, existed.back() ?
                std::string(item["before"].GetString(), item["before"].GetStringLength()) : ""});
        }
        if (originals.back().path != anchor)
            throw std::runtime_error("Transaction does not belong to this project.");
        if (originals.size() == 2) {
            // A record may only restore the anchor and its declared scene.
            rapidjson::Document project;
            const auto& after = files[1]["after"];
            project.Parse(after.GetString(), after.GetStringLength());
            if (project.HasParseError() || !project.IsObject() || !project.HasMember("scene") ||
                !project["scene"].IsString()) throw std::runtime_error("Invalid transaction scene.");
            const fs::path scene(project["scene"].GetString());
            if ((anchor.parent_path() / scene).lexically_normal() != originals[0].path ||
                originals[0].path == anchor || originals[0].path == journal ||
                originals[0].path == temporary_path(anchor))
                throw std::runtime_error("Transaction scene does not match the project.");
        }
        for (std::size_t index = 0; index < originals.size(); ++index) {
            if (existed[index]) { stage(originals[index]); replace(originals[index].path); }
            else fs::remove(originals[index].path);
            if (observe) observe("restored", index);
        }
        remove_journal(journal);
        clean_temporaries(originals);
        return true;
    } catch (const std::exception& exception) {
        error = "Project recovery failed: " + std::string(exception.what());
        return false;
    }
}

inline bool save(const fs::path& input_anchor, const std::vector<document>& documents,
                 std::string& error, const checkpoint& observe = {}) {
    fs::path anchor, journal;
    bool journal_published = false;
    std::vector<document> staged;
    try {
        anchor = fs::absolute(input_anchor).lexically_normal();
        journal = journal_path(anchor);
        if (!recover(anchor, error)) return false;
        if (documents.empty() || documents.size() > 2 || documents.back().path != anchor)
            throw std::runtime_error("Invalid save transaction.");
        std::set<fs::path> reserved{journal, temporary_path(journal)};
        for (const auto& doc : documents) {
            if (!doc.path.is_absolute() || doc.path != doc.path.lexically_normal() ||
                !reserved.insert(doc.path).second ||
                !reserved.insert(temporary_path(doc.path)).second)
                throw std::runtime_error("Project and scene paths overlap.");
        }
        rapidjson::Document record;
        record.SetObject();
        auto& allocator = record.GetAllocator();
        record.AddMember("version", 1, allocator);
        rapidjson::Value entries(rapidjson::kArrayType);
        for (const auto& doc : documents) {
            if (doc.path == journal || doc.path == temporary_path(anchor) ||
                (documents.size() == 2 && documents.front().path == anchor))
                throw std::runtime_error("Project and scene paths overlap.");
            rapidjson::Value entry(rapidjson::kObjectType), before;
            if (!regular_or_absent(doc.path)) throw std::runtime_error("Invalid save destination.");
            if (fs::exists(doc.path)) {
                const auto previous = read(doc.path);
                before.SetString(previous.data(), static_cast<rapidjson::SizeType>(previous.size()), allocator);
            }
            const auto name = doc.path.string();
            entry.AddMember("path", rapidjson::Value(name.c_str(), allocator), allocator);
            entry.AddMember("before", before, allocator);
            entry.AddMember("after", rapidjson::Value(doc.contents.data(),
                static_cast<rapidjson::SizeType>(doc.contents.size()), allocator), allocator);
            entries.PushBack(entry, allocator);
            // Record ownership only after validation; never delete a blocking directory.
            if (!regular_or_absent(temporary_path(doc.path)))
                throw std::runtime_error("Invalid temporary destination.");
            staged.push_back(doc);
            stage(doc);
            if (observe) observe("staged", staged.size() - 1);
        }
        record.AddMember("files", entries, allocator);
        stage({journal, encode(record)});
        replace(journal);
        journal_published = true;
        if (observe) observe("journaled", 0);
        for (std::size_t index = 0; index < documents.size(); ++index) {
            replace(documents[index].path);
            if (observe) observe("replaced", index);
        }
        // Removing the journal is the commit point. Before it, recovery rolls back.
        remove_journal(journal);
        if (observe) observe("committed", 0);
        return true;
    } catch (const std::exception& exception) {
        error = "Could not save project: " + std::string(exception.what());
        if (journal_published) {
            std::string recovery_error;
            if (!recover(anchor, recovery_error)) error += " " + recovery_error;
        } else clean_temporaries(staged);
        return false;
    }
}
}
