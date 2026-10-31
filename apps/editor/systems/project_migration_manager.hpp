#pragma once
#include "project_file_transaction.hpp"
#include "util/semantic_version.hpp"
#include "util/version.hpp"

namespace editor::ui {
class project_migration_manager {
public:
    static constexpr unsigned current_format = 2;
    static void add_manifest(rapidjson::Document& doc, const std::string& project_version = "0.1.0") {
        auto& allocator = doc.GetAllocator();
        doc.AddMember("engine_version", rapidjson::Value(BUBBLE_ENGINE_VERSION, allocator), allocator);
        doc.AddMember("minimum_engine_version", rapidjson::Value(BUBBLE_ENGINE_VERSION, allocator), allocator);
        doc.AddMember("project_version", rapidjson::Value(project_version.c_str(), allocator), allocator);
    }
    static bool compatible(const rapidjson::Document& doc, std::string& error) {
        for (const char* key : {"engine_version", "minimum_engine_version", "project_version"}) {
            if (!doc.HasMember(key) || !doc[key].IsString() ||
                !commons::semantic_version::parse({doc[key].GetString(), doc[key].GetStringLength()})) {
                error = std::string("Invalid project manifest field: ") + key;
                return false;
            }
        }
        const auto current = *commons::semantic_version::parse(BUBBLE_ENGINE_VERSION);
        const auto minimum = *commons::semantic_version::parse(doc["minimum_engine_version"].GetString());
        const auto engine = *commons::semantic_version::parse(doc["engine_version"].GetString());
        if (minimum > current || engine > current) {
            error = "This project requires a newer Bubble Engine. Update the engine before opening it.";
            return false;
        }
        return true;
    }
    // Every format transition is applied to memory; callers validate all scene data
    // and resources before committing the converted manifest.
    static bool convert(rapidjson::Document& doc, bool& changed, std::string& error) {
        changed = false;
        if (!doc.HasMember("version") || !doc["version"].IsUint() ||
            doc["version"].GetUint() < 1 || doc["version"].GetUint() > current_format) {
            error = "Unsupported project format; update the engine before opening this project.";
            return false;
        }
        while (doc["version"].GetUint() < current_format) {
            switch (doc["version"].GetUint()) {
            case 1:
                for (const char* key : {"engine_version", "minimum_engine_version", "project_version"}) {
                    if (doc.HasMember(key)) {
                        error = "Legacy project has conflicting manifest fields; files were preserved.";
                        return false;
                    }
                }
                add_manifest(doc);
                doc["version"].SetUint(2);
                changed = true;
                break;
            default: error = "Missing migration step."; return false;
            }
        }
        return compatible(doc, error);
    }
    static bool commit(const std::filesystem::path& project, const rapidjson::Document& converted,
                       std::string& error, const std::string* expected_contents = nullptr) {
        namespace fs = std::filesystem;
        try {
            const auto anchor = fs::absolute(project).lexically_normal();
            if (expected_contents && project_files::read(anchor) != *expected_contents)
                throw std::runtime_error("Project changed while loading; reopen it before migrating.");
            fs::path backup;
            for (unsigned index = 0;; ++index) {
                if (index == 10000) throw std::runtime_error("Too many migration backups.");
                backup = fs::path(anchor.string() + ".migration-backup-" + std::to_string(index));
                if (fs::create_directory(backup)) break;
            }
            // Only the manifest changes in 1 -> 2. Preserve its exact preimage;
            // scene/assets remain untouched by this transition.
            if (!project_files::regular_or_absent(anchor) || !fs::is_regular_file(anchor))
                throw std::runtime_error("Unsafe project manifest.");
            const auto destination = backup / anchor.filename();
            fs::copy_file(anchor, destination);
            if (project_files::read(anchor) != project_files::read(destination))
                throw std::runtime_error("Backup verification failed.");
            const auto output = project_files::encode(converted);
            rapidjson::Document check;
            check.Parse(output.data(), output.size());
            if (check.HasParseError() || check != converted || !compatible(check, error))
                throw std::runtime_error("Converted manifest validation failed.");
            return project_files::save(anchor, {{anchor, output}}, error);
        } catch (const std::exception& exception) {
            error = "Project migration failed; original files preserved: " + std::string(exception.what());
            return false;
        }
    }
};
}
