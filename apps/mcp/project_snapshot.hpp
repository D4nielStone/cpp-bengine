#pragma once

#include <rapidjson/document.h>

#include <filesystem>

namespace bubble::mcp {

struct project_snapshot {
    rapidjson::Document manifest;
    rapidjson::Document scene;

    explicit project_snapshot(const std::filesystem::path& project);
    const rapidjson::Value& entities() const;
};

}
