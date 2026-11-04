#pragma once

#include "asset_file_operations.hpp"
#include "loaders/material_asset.hpp"
#include "util/pbr_material.hpp"

namespace editor::ui {
inline bool is_material_file(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".bmat";
}

inline std::string encode_material_edit(const commons::material& material,
    const std::filesystem::path& directory, const std::string& original) {
    // The general encoder prepares sidecar references for embedded pixels.
    // Keep existing inline textures intact when editing their material in place.
    auto external = material;
    std::erase_if(external.textures, [](const auto& entry) { return bool(entry.second.embedded); });
    const auto encoded = commons::encode_material_asset(external, directory);
    rapidjson::Document updated, previous;
    updated.Parse(encoded.data(), encoded.size());
    previous.Parse(original.data(), original.size());
    if (previous.HasParseError() || !previous.IsObject() || !previous.HasMember("textures") ||
        !previous["textures"].IsObject() || !previous.HasMember("uniforms") ||
        !previous["uniforms"].IsObject())
        throw std::runtime_error("O arquivo original do material é inválido.");
    auto& maps = updated["textures"];
    for (const auto& [name, texture] : material.textures) {
        if (!texture.embedded) continue;
        const auto existing = previous["textures"].FindMember(name.c_str());
        if (existing == previous["textures"].MemberEnd())
            throw std::runtime_error("Textura embutida ausente no arquivo original.");
        auto& allocator = updated.GetAllocator();
        maps.AddMember(rapidjson::Value(existing->name, allocator),
            rapidjson::Value(existing->value, allocator), allocator);
    }
    auto& allocator = previous.GetAllocator();
    previous["uniforms"].CopyFrom(updated["uniforms"], allocator);
    previous["textures"].CopyFrom(maps, allocator);
    return project_files::encode(previous);
}

namespace detail {
class material_save_lock {
public:
    explicit material_save_lock(const std::filesystem::path& path)
        : m_path(path.parent_path() / ("." + path.filename().string() + ".saving")) {
        if (!std::filesystem::create_directory(m_path))
            throw std::runtime_error("Já existe uma gravação pendente para este material.");
    }
    ~material_save_lock() {
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }
    material_save_lock(const material_save_lock&) = delete;
    material_save_lock& operator=(const material_save_lock&) = delete;
private:
    std::filesystem::path m_path;
};
}

inline std::filesystem::path create_material_file(const std::filesystem::path& directory,
    const std::filesystem::path& root, std::string name) {
    if (name.empty() || name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
        name.starts_with('.') || name.find('\0') != std::string::npos ||
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }))
        throw std::runtime_error("Escolha um nome para o material.");

    std::filesystem::path filename(name);
    if (!filename.has_extension()) filename += ".bmat";
    if (!is_material_file(filename)) throw std::runtime_error("Use a extensão .bmat.");
    const auto folder = std::filesystem::canonical(directory);
    if (!path_inside(folder, std::filesystem::canonical(root)) || !std::filesystem::is_directory(folder))
        throw std::runtime_error("Escolha uma pasta dentro do projeto.");
    const auto path = folder / filename;
    const detail::material_save_lock lock(path);
    if (std::filesystem::exists(std::filesystem::symlink_status(path)))
        throw std::runtime_error("Já existe um arquivo com esse nome.");

    commons::material material;
    material.set_uniform("material.albedo", commons::color{.7f, .7f, .7f, 1.f});
    for (const auto& factor : commons::pbr_factors)
        material.set_uniform(std::string("material.") + factor.name, factor.fallback);
    const auto temporary = project_files::temporary_path(path);
    if (std::filesystem::exists(std::filesystem::symlink_status(temporary)))
        throw std::runtime_error("Já existe uma gravação temporária pendente.");
    project_files::stage({path, commons::encode_material_asset(material, folder)});
    std::error_code result;
    std::filesystem::create_hard_link(temporary, path, result);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    if (result) throw std::filesystem::filesystem_error("Não foi possível criar o material", path, result);
    return path;
}

inline void save_material_file(const std::filesystem::path& path, const std::filesystem::path& root,
    const std::string& expected, const std::string& contents) {
    if (asset_files::editable_file(path, root) != path)
        throw std::runtime_error("Escolha um arquivo de material dentro do projeto.");
    const detail::material_save_lock lock(path);
    if (project_files::read(path) != expected)
        throw std::runtime_error("O arquivo mudou. Selecione o material novamente para editar.");
    if (std::filesystem::exists(std::filesystem::symlink_status(project_files::temporary_path(path))))
        throw std::runtime_error("Já existe uma gravação temporária pendente.");
    try {
        project_files::stage({path, contents});
        if (project_files::read(path) != expected)
            throw std::runtime_error("O arquivo mudou durante a gravação.");
        project_files::replace(path);
    } catch (...) {
        project_files::clean_temporaries({{path, {}}});
        throw;
    }
}
}
