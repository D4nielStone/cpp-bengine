#pragma once
#include "distribution_paths.hpp"
#include "project_file_transaction.hpp"

namespace editor::ui {
inline bool visual_scripting_enabled() {
    const auto path = editor::distribution::user_path("editor-features.json");
    try {
        if (!std::filesystem::exists(path)) return false;
        rapidjson::Document value;
        value.Parse(project_files::read(path).c_str());
        return value.IsObject() && value.HasMember("visual_scripting") &&
            value["visual_scripting"].IsBool() && value["visual_scripting"].GetBool();
    } catch (...) { return false; }
}
inline void save_visual_scripting_enabled(bool enabled) {
    rapidjson::Document value(rapidjson::kObjectType);
    value.AddMember("visual_scripting", enabled, value.GetAllocator());
    const auto path = editor::distribution::user_path("editor-features.json");
    project_files::stage({path, project_files::encode(value)});
    project_files::replace(path);
}
}
