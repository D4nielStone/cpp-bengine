#pragma once
#include <rapidjson/document.h>
#include <resources/executable_archive.hpp>
#include <os/resource_files.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace game_bootstrap {
namespace fs = std::filesystem;
struct launch { fs::path directory; fs::path project; };

inline fs::path executable_path(const fs::path& argument) {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) throw std::runtime_error("Could not locate the game executable.");
    path.resize(length);
    return fs::path(path);
#else
    std::error_code error;
    const auto executable = fs::read_symlink("/proc/self/exe", error);
    return error ? fs::absolute(argument) : executable;
#endif
}

inline fs::path executable_directory(const fs::path& argument) { return executable_path(argument).parent_path(); }
inline std::string key(const fs::path& path, const fs::path& root) {
    const auto relative = path.is_absolute() ? path.lexically_normal().lexically_relative(root) : path.lexically_normal();
    const auto name = relative.generic_string();
    return executable_archive::safe_name(name) ? name : std::string{};
}
inline bool mount_executable(const fs::path& executable) {
    const auto archive = executable_archive::reader::open(executable);
    if (!archive) {
        if (executable_archive::required()) throw std::runtime_error("The embedded game package is missing or truncated.");
        return false;
    }
    const auto root = fs::absolute(executable).parent_path().lexically_normal();
    bgui::resource_files::mounted = {
        [archive, root](const fs::path& path) { return archive->contains(key(path, root)); },
        [archive, root](const fs::path& path) { return archive->read(key(path, root)); }
    };
    return true;
}
inline bool diagnostics_enabled(const fs::path& directory) {
    return !bgui::resource_files::embedded() && !bgui::resource_files::exists(directory / "game.json");
}

// Resolve everything before changing working directory or creating a GPU context.
inline launch resolve(const fs::path& directory) {
    auto input = bgui::resource_files::stream(directory / "game.json");
    if (!input) throw std::runtime_error("Could not open game.json.");
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document document;
    document.Parse(contents.data(), contents.size());
    if (input.bad() || document.HasParseError() || !document.IsObject() ||
        !document.HasMember("format") || !document["format"].IsString() ||
        std::string(document["format"].GetString()) != "bubble-engine-game" ||
        !document.HasMember("version") || !document["version"].IsInt() || document["version"].GetInt() != 1 ||
        !document.HasMember("project") || !document["project"].IsString())
        throw std::runtime_error("Invalid game.json.");
    const fs::path relative(document["project"].GetString());
    const auto root = bgui::resource_files::embedded() ? fs::absolute(directory).lexically_normal() : fs::canonical(directory);
    const auto project = bgui::resource_files::embedded() ? (root / relative).lexically_normal() : fs::weakly_canonical(root / relative);
    const auto contained = project.lexically_relative(root);
    if (relative.is_absolute() || contained.empty() || *contained.begin() == ".." || !bgui::resource_files::exists(project))
        throw std::runtime_error("Game entry project is missing or outside the package.");
    return {root, project};
}
}
