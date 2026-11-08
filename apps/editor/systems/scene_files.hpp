#pragma once

#include "project_file_transaction.hpp"

#include <algorithm>
#include <cctype>

namespace editor::ui::scene_files {
namespace fs = std::filesystem;

inline bool is_scene(const fs::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return std::tolower(character); });
    return extension == ".scene" || extension == ".world";
}

inline bool inside(const fs::path& path, const fs::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && relative != "." && *relative.begin() != "..";
}

inline fs::path checked_scene(const fs::path& root, const fs::path& path) {
    const auto target = fs::canonical(path);
    if (!is_scene(path) || !inside(target, fs::canonical(root)) ||
        !fs::is_regular_file(fs::symlink_status(path)))
        throw std::runtime_error("Escolha um arquivo de cena dentro do projeto.");
    return target;
}

inline std::vector<fs::path> list(const fs::path& root, bool recursive) {
    std::vector<fs::path> scenes;
    for (auto it = fs::recursive_directory_iterator(root);
         it != fs::recursive_directory_iterator(); ++it) {
        if (!recursive || it->path().filename().string().starts_with('.'))
            it.disable_recursion_pending();
        if (is_scene(it->path()) && fs::is_regular_file(it->symlink_status()))
            scenes.push_back(it->path());
    }
    std::sort(scenes.begin(), scenes.end());
    return scenes;
}

inline void create(const fs::path& root, const std::string& name, const fs::path& source = {}) {
    if (name.empty() || name.front() == '.' || name.front() == ' ' || name.back() == ' ' ||
        name.find_first_of("/\\:*?\"<>|") != std::string::npos ||
        std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32; }))
        throw std::runtime_error("Digite um nome de cena válido, sem caminhos.");
    auto target = root / name;
    if (!is_scene(target))
        target += ".scene";
    if (fs::exists(fs::symlink_status(target)))
        throw std::runtime_error("Já existe um arquivo com esse nome.");

    std::string contents = R"({"format":"bubble-engine-scene","version":1,"entities":[]})";
    if (!source.empty())
        contents = project_files::read(checked_scene(root, source));
    std::string error;
    if (!project_files::save(target, {{target, std::move(contents)}}, error))
        throw std::runtime_error(error);
}

inline void check_deletion(const fs::path& root, const fs::path& path, const fs::path& active) {
    const auto target = checked_scene(root, path);
    if (!active.empty() && fs::weakly_canonical(active) == target)
        throw std::runtime_error("Abra outra cena antes de excluir a cena ativa.");

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.path().extension() != ".bengine" || !entry.is_regular_file())
            continue;
        const auto contents = project_files::read(entry.path());
        rapidjson::Document document;
        document.Parse(contents.data(), contents.size());
        if (document.HasParseError() || !document.IsObject() ||
            (document.HasMember("scene") && !document["scene"].IsString()))
            throw std::runtime_error("Verifique o arquivo de projeto " + entry.path().string() + ".");
        if (document.HasMember("scene") &&
            fs::weakly_canonical(entry.path().parent_path() / document["scene"].GetString()) == target)
            throw std::runtime_error("A cena é usada como inicial por " + entry.path().filename().string() + ".");
    }
}
}
