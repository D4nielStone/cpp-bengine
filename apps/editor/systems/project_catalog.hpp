#pragma once
#include "project_file_transaction.hpp"
#include "asset_import.hpp"
#include <fstream>

namespace editor::ui {
inline void forget_project(const std::filesystem::path& catalog, const std::filesystem::path& project) {
    std::ifstream input(catalog);
    std::string remaining;
    const auto requested = std::filesystem::absolute(project).lexically_normal();
    for (std::string line; std::getline(input, line);)
        if (!line.empty() && std::filesystem::absolute(line).lexically_normal() != requested)
            remaining += line + "\n";
    project_files::stage({catalog, remaining});
    project_files::replace(catalog);
}

inline void erase_project(const std::filesystem::path& project, const std::filesystem::path& active) {
    namespace fs = std::filesystem;
    const auto file = fs::canonical(project);
    const auto directory = file.parent_path();
    if (file.extension() != ".bengine" || !fs::is_regular_file(file) ||
        directory == directory.root_path() || directory == fs::current_path() ||
        path_inside(fs::current_path(), directory))
        throw std::runtime_error("Não é possível apagar esta pasta de projeto.");
    if (!active.empty()) {
        const auto active_file = fs::weakly_canonical(active);
        if (active_file.parent_path() == directory || path_inside(active_file, directory))
            throw std::runtime_error("Abra outro projeto antes de apagar o projeto ativo.");
    }
    rapidjson::Document document;
    const auto contents = project_files::read(file);
    document.Parse(contents.c_str());
    if (document.HasParseError() || !document.IsObject() || !document.HasMember("format") ||
        !document["format"].IsString() || std::string(document["format"].GetString()) != "bubble-engine-project")
        throw std::runtime_error("O arquivo não é um projeto Bubble Engine válido.");
    if (fs::absolute(project).lexically_normal() != file)
        throw std::runtime_error("Abra o caminho real do projeto antes de apagar a pasta.");
    fs::remove_all(directory);
}
}
