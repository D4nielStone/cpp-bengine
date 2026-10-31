#pragma once
#include <rapidjson/document.h>
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

inline fs::path executable_directory(const fs::path& argument) {
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) throw std::runtime_error("Could not locate the game executable.");
    path.resize(length);
    return fs::path(path).parent_path();
#else
    std::error_code error;
    const auto executable = fs::read_symlink("/proc/self/exe", error);
    return (error ? fs::absolute(argument) : executable).parent_path();
#endif
}

// Resolve everything before changing working directory or creating a GPU context.
inline launch resolve(const fs::path& directory) {
    std::ifstream input(directory / "game.json", std::ios::binary);
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
    const auto root = fs::canonical(directory);
    const auto project = fs::weakly_canonical(root / relative);
    const auto contained = project.lexically_relative(root);
    if (relative.is_absolute() || contained.empty() || *contained.begin() == ".." || !fs::is_regular_file(project))
        throw std::runtime_error("Game entry project is missing or outside the package.");
    return {root, project};
}
}
