#pragma once
#include "util/semantic_version.hpp"
#include "util/version.hpp"
#include <rapidjson/document.h>
#include <string>
#include <optional>

namespace editor::ui {
inline std::string release_status(const std::string& response) {
    rapidjson::Document doc;
    doc.Parse(response.data(), response.size());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("tag_name") ||
        !doc["tag_name"].IsString() || !doc.HasMember("draft") || !doc["draft"].IsBool() ||
        !doc.HasMember("prerelease") || !doc["prerelease"].IsBool() ||
        doc["draft"].GetBool() || doc["prerelease"].GetBool())
        return "Update check unavailable";
    std::string tag(doc["tag_name"].GetString(), doc["tag_name"].GetStringLength());
    if (tag.empty() || tag.front() != 'v') return "Update check unavailable";
    const auto latest = commons::semantic_version::parse(std::string_view(tag).substr(1));
    if (!latest) return "Update check unavailable";
    if (*latest > *commons::semantic_version::parse(BUBBLE_ENGINE_VERSION))
        return "Update available: " + tag;
    return "Engine is up to date";
}
std::optional<int> launch_managed_editor(int argc, char** argv);
std::string check_latest_release();
void open_engine_releases();
}
