#pragma once
#include "distribution_paths.hpp"
#include "../../project/project_file_transaction.hpp"
#include <iostream>
#include <algorithm>
#include <map>

namespace editor::ui {
class localization {
public:
    explicit localization(const std::filesystem::path& directory, std::string language = "en-US")
        : m_language(valid_language(language) ? std::move(language) : "en-US") {
        load(directory / "en-US.json", m_fallback);
        if (m_language != "en-US") load(directory / (m_language + ".json"), m_strings);
    }
    std::string text(const std::string& key) const {
        if (auto it = m_strings.find(key); it != m_strings.end()) return it->second;
        if (auto it = m_fallback.find(key); it != m_fallback.end()) return it->second;
        return key;
    }
    const std::string& language() const { return m_language; }
    static bool valid_language(const std::string& language) {
        return !language.empty() && language.size() <= 48 &&
            std::all_of(language.begin(), language.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
            });
    }
    static std::string read_preference(const std::filesystem::path& path) {
        try {
            std::string error;
            if (!project_files::recover(path, error)) return "en-US";
            rapidjson::Document doc;
            const auto contents = project_files::read(path);
            doc.Parse(contents.data(), contents.size());
            if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("language") &&
                doc["language"].IsString() && valid_language(doc["language"].GetString()))
                return doc["language"].GetString();
        } catch (const std::exception&) {}
        return "en-US";
    }
    static bool save_preference(const std::filesystem::path& input_path,
        const std::string& language, std::string& error) {
        if (!valid_language(language)) { error = "Unsupported language"; return false; }
        std::error_code path_error;
        const auto path = std::filesystem::absolute(input_path, path_error).lexically_normal();
        if (path_error) { error = path_error.message(); return false; }
        if (!project_files::recover(path, error)) return false;
        rapidjson::Document doc;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            try {
                const auto contents = project_files::read(path);
                doc.Parse(contents.data(), contents.size());
                if (doc.HasParseError() || !doc.IsObject()) {
                    error = "Invalid editor preferences; file preserved."; return false;
                }
            } catch (const std::exception& e) { error = e.what(); return false; }
        } else if (ec) { error = ec.message(); return false; }
        else doc.SetObject();
        if (doc.HasMember("language")) doc.RemoveMember("language");
        doc.AddMember("language", rapidjson::Value(language.c_str(), doc.GetAllocator()), doc.GetAllocator());
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        return project_files::save(path, {{path, buffer.GetString()}}, error);
    }
private:
    std::string m_language;
    std::map<std::string, std::string> m_strings, m_fallback;
    static void load(const std::filesystem::path& path, std::map<std::string, std::string>& target) {
        try {
            const auto contents = project_files::read(path);
            rapidjson::Document doc;
            doc.Parse<rapidjson::kParseValidateEncodingFlag>(contents.data(), contents.size());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("strings") || !doc["strings"].IsObject())
                throw std::runtime_error("Invalid language catalog");
            std::map<std::string, std::string> candidate;
            for (const auto& entry : doc["strings"].GetObject()) {
                if (!entry.value.IsString()) throw std::runtime_error("Invalid translation value");
                candidate.emplace(entry.name.GetString(), entry.value.GetString());
            }
            target = std::move(candidate);
        } catch (const std::exception& e) { std::cerr << "[editor] " << path << ": " << e.what() << '\n'; }
    }
};
inline std::filesystem::path language_catalog_directory() {
    return editor::distribution::path("assets/locales", COMMONS_ASSET_DIR "/locales");
}
inline std::vector<std::string> available_languages() {
    std::vector<std::string> result{"en-US"};
    std::error_code error;
    std::filesystem::directory_iterator it(language_catalog_directory(), error), end;
    for (; !error && it != end; it.increment(error)) {
        if (it->path().extension() != ".json") continue;
        const auto code = it->path().stem().string();
        if (code != "en-US" && localization::valid_language(code)) result.push_back(code);
    }
    std::sort(result.begin() + 1, result.end());
    return result;
}
inline std::filesystem::path language_preference_path() {
    return editor::distribution::user_path("editor-preferences.json");
}
// One immutable catalog per process; changing the saved default takes effect on restart.
inline const localization& editor_language() {
    static const localization catalog(
        language_catalog_directory(),
        localization::read_preference(language_preference_path()));
    return catalog;
}
inline std::string tr(const std::string& key) { return editor_language().text(key); }
}
