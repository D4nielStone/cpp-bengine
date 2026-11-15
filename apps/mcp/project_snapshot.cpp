#include <ranges>
#include "project_snapshot.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

namespace bubble::mcp {
namespace {
namespace fs = std::filesystem;

constexpr auto parse_flags = rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag;

bool within_depth_limit(const rapidjson::Value& value, unsigned depth) {
    if (depth == 0)
        return false;
    if (value.IsObject()) {
        for (const auto& member : std::ranges::subrange(value.MemberBegin(), value.MemberEnd()))
            if (!within_depth_limit(member.value, depth - 1))
                return false;
    } else if (value.IsArray()) {
        for (const auto& item : value.GetArray())
            if (!within_depth_limit(item, depth - 1))
                return false;
    }
    return true;
}

rapidjson::Document read_document(const fs::path& file, std::string_view format) {
    constexpr std::uintmax_t max_bytes = 32 * 1024 * 1024;
    if (!fs::is_regular_file(file))
        throw std::runtime_error("Expected a regular project or scene file.");
    const auto size = fs::file_size(file);
    if (size > max_bytes)
        throw std::runtime_error("Project or scene exceeds the 32 MiB inspection limit.");

    std::ifstream input(file, std::ios::binary);
    std::string contents(static_cast<std::size_t>(size), '\0');
    if (!input.read(contents.data(), static_cast<std::streamsize>(size)) ||
        input.peek() != std::char_traits<char>::eof() || input.bad())
        throw std::runtime_error("Could not read the saved file. Retry after saving finishes.");

    rapidjson::Document document;
    document.Parse<parse_flags>(contents.data(), contents.size());
    if (contents.find('\0') != std::string::npos || document.HasParseError() ||
        !document.IsObject() || !document.HasMember("format") ||
        !document["format"].IsString() || std::string_view(document["format"].GetString(), document["format"].GetStringLength()) != format ||
        !document.HasMember("version") || !document["version"].IsUint())
        throw std::runtime_error("Expected a valid " + std::string(format) + " JSON document.");
    if (!within_depth_limit(document, 64))
        throw std::runtime_error("Project or scene JSON nesting exceeds the inspection limit.");
    return document;
}

}

project_snapshot::project_snapshot(const fs::path& project) {
    const auto root = project.parent_path();
    if (fs::canonical(project).parent_path() != root)
        throw std::runtime_error("Project file resolves outside the configured directory.");
    manifest = read_document(project, "bubble-engine-project");

    if (manifest.HasMember("scene")) {
        const auto& reference = manifest["scene"];
        if (!reference.IsString())
            throw std::runtime_error("Invalid main scene reference.");
        const std::string_view name(reference.GetString(), reference.GetStringLength());
        if (name.empty() || name.find('\0') != std::string_view::npos)
            throw std::runtime_error("Invalid main scene reference.");

        const auto path = fs::canonical(root / fs::path(std::u8string(name.begin(), name.end())));
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || *relative.begin() == "..")
            throw std::runtime_error("Main scene resolves outside the configured project directory.");
        scene = read_document(path, "bubble-engine-scene");
    }

    const auto& source = scene.IsNull() ? manifest : scene;
    if (!source.HasMember("entities") || !source["entities"].IsArray())
        throw std::runtime_error("The saved scene is missing an entity list.");

    std::unordered_set<unsigned> ids;
    for (const auto& entity : entities().GetArray()) {
        if (!entity.IsObject() || !entity.HasMember("id") || !entity["id"].IsUint() ||
            entity["id"].GetUint() == 0 || !ids.insert(entity["id"].GetUint()).second ||
            (entity.HasMember("name") && !entity["name"].IsString()) ||
            (entity.HasMember("parent") && !entity["parent"].IsUint()) ||
            !entity.HasMember("components") || !entity["components"].IsObject())
            throw std::runtime_error("The saved scene contains an invalid or duplicate entity.");
    }
}

const rapidjson::Value& project_snapshot::entities() const {
    return (scene.IsNull() ? manifest : scene)["entities"];
}

}
