#pragma once
#include "asset_import.hpp"
#include "project_file_transaction.hpp"
#include <sstream>
#include <exception>
#include <string_view>

namespace editor::ui {
namespace asset_files {
namespace fs = std::filesystem;

// Returns the canonical path of a file inside the project, or throws an exception if the file is not editable.
inline fs::path editable_file(const fs::path& requested, const fs::path& project) {
    const auto root = fs::canonical(project);
    const auto source = fs::canonical(requested);
    if (!path_inside(source, root) || !fs::is_regular_file(source) || fs::is_symlink(requested))
        throw std::runtime_error("Escolha um arquivo dentro do projeto.");
    if (source.extension() == ".bengine" || source.filename().string().starts_with('.'))
        throw std::runtime_error("O arquivo de configuração do projeto não pode ser movido ou excluído.");
    return source;
}

// Returns true if the path is a scene document (".scene", ".world" or ".bengine").
inline bool scene_document(const fs::path& path) {
    const auto ext = path.extension().string();
    return ext == ".scene" || ext == ".world" || ext == ".bengine";
}

// Rewrites all paths in a JSON document, returning true if any changes were made.
inline bool rewrite_paths(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
    const fs::path& root, const fs::path& from, const fs::path& to, bool path_value = false) {
    bool changed = false;
    if (value.IsString() && path_value && value.GetStringLength()) {
        const fs::path stored(value.GetString());
        const auto absolute = (stored.is_absolute() ? stored : root / stored).lexically_normal();
        if (absolute == from) {
            const auto replacement = stored.is_absolute() ? to : to.lexically_relative(root);
            value.SetString(replacement.generic_string().c_str(), allocator);
            return true;
        }
    } else if (value.IsObject()) {
        for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
            const std::string_view key(it->name.GetString(), it->name.GetStringLength());
            const bool image_value = key == "value" && value.HasMember("type") &&
                value["type"].IsString() && std::string_view(value["type"].GetString()) == "image";
            const bool is_path = image_value || path_value || key == "scene" || key == "model" ||
                key == "directory" || key == "scripts" || key == "maps";
            changed |= rewrite_paths(it->value, allocator, root, from, to, is_path);
        }
    } else if (value.IsArray()) {
        for (auto& item : value.GetArray())
            changed |= rewrite_paths(item, allocator, root, from, to, path_value);
    }
    return changed;
}

inline std::string rewrite_document(const std::string& contents, const fs::path& root,
    const fs::path& from, const fs::path& to) {
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError())
        throw std::runtime_error("Há um documento de cena/projeto com JSON inválido. Corrija-o antes de mover arquivos.");
    if (!rewrite_paths(document, document.GetAllocator(), root, from, to)) return contents;
    return project_files::encode(document);
}

inline fs::path move(const fs::path& requested, const fs::path& folder,
    const fs::path& project, bool model) {
    const auto root = fs::canonical(project);
    const auto source = editable_file(requested, root);
    const auto directory = fs::canonical(folder);
    if (!fs::is_directory(directory) || !path_inside(directory, root))
        throw std::runtime_error("Escolha uma pasta dentro do projeto.");
    const auto target = directory / source.filename();
    if (target == source) return source;
    if (fs::exists(target)) throw std::runtime_error("Já existe um arquivo com esse nome na pasta de destino.");

    struct replacement { fs::path path; std::string before, after; };
    std::vector<replacement> changes;
    bool main_scene_moved = false;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.is_symlink() || !scene_document(entry.path())) continue;
        const auto before = project_files::read(entry.path());
        const auto after = rewrite_document(before, root, source, target);
        if (before != after) {
            changes.push_back({entry.path() == source ? target : entry.path(), before, after});
            main_scene_moved |= entry.path().extension() == ".bengine" && scene_document(source);
        }
    }
    const auto marker = root / ".bengine-main-scene";
    const bool marker_exists = fs::exists(marker);
    const auto old_marker = marker_exists ? project_files::read(marker) : std::string{};
    std::istringstream marker_input(old_marker);
    std::string marker_value;
    std::getline(marker_input, marker_value);
    const bool update_marker = main_scene_moved ||
        (!marker_value.empty() && (root / marker_value).lexically_normal() == source);
    if (update_marker) changes.push_back({marker, old_marker, target.lexically_relative(root).generic_string() + "\n"});

    const auto dependencies = asset_dependencies(source, model);
    std::vector<fs::path> copied, temporaries;
    bool renamed = false;
    std::size_t replaced = 0;
    try {
        for (const auto& dependency : dependencies) {
            if (dependency == source) continue;
            const auto copy = (directory / dependency.lexically_relative(source.parent_path())).lexically_normal();
            if (!path_inside(copy, root))
                throw std::runtime_error("Uma dependência do modelo sairia da pasta do projeto.");
            if (copy == dependency) continue;
            if (fs::exists(copy)) {
                std::ifstream before(dependency, std::ios::binary), existing(copy, std::ios::binary);
                if (before && existing && fs::is_regular_file(copy) && fs::file_size(copy) == fs::file_size(dependency) &&
                    std::equal(std::istreambuf_iterator<char>(before), std::istreambuf_iterator<char>(),
                        std::istreambuf_iterator<char>(existing))) continue;
                throw std::runtime_error("Uma dependência do modelo já existe no destino: " + copy.filename().string());
            }
            fs::create_directories(copy.parent_path());
            if (!path_inside(fs::canonical(copy.parent_path()), root))
                throw std::runtime_error("Uma dependência aponta para fora do projeto.");
            fs::copy_file(dependency, copy);
            copied.push_back(copy);
        }
        for (const auto& change : changes) {
            const auto temporary = project_files::temporary_path(change.path);
            if (fs::exists(fs::symlink_status(temporary)))
                throw std::runtime_error("Já existe uma gravação temporária pendente: " + temporary.string());
            temporaries.push_back(temporary);
            project_files::stage({change.path, change.after});
        }
        fs::rename(source, target);
        renamed = true;
        for (const auto& change : changes) {
            project_files::replace(change.path);
            ++replaced;
        }
    } catch (...) {
        const auto failure = std::current_exception();
        for (std::size_t i = 0; i < replaced; ++i) {
            const auto& change = changes[i];
            if (change.path == marker && !marker_exists) fs::remove(marker);
            else { project_files::stage({change.path, change.before}); project_files::replace(change.path); }
        }
        if (renamed) fs::rename(target, source);
        std::error_code ignored;
        for (const auto& copy : copied) fs::remove(copy, ignored);
        for (const auto& temporary : temporaries) fs::remove(temporary, ignored);
        std::rethrow_exception(failure);
    }
    return target;
}

inline void erase(const fs::path& requested, const fs::path& project) {
    const auto source = editable_file(requested, project);
    if (!fs::remove(source)) throw std::runtime_error("O arquivo não foi encontrado.");
}
}
}
