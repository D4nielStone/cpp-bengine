#pragma once

#include "editable_model_import.hpp"

#include <atomic>
#include <chrono>

namespace editor::ui {

inline std::filesystem::path extract(
    const std::filesystem::path& project_root, const std::filesystem::path& original,
    std::string name,
    const commons::editable_mesh_data& mesh, const commons::material& material
) {
    if (is_native_model(name)) name = std::filesystem::path(name).stem().string();
    if (name.empty() || name.front() == '.' || name.back() == '.' || name.back() == ' ' ||
        name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
        std::ranges::any_of(name, [](unsigned char c) { return c < 32; })) {
        throw std::runtime_error("Escolha um nome válido para o arquivo .bmodel.");
    }
    if (mesh.faces.empty()) throw std::runtime_error("A malha precisa ter faces para ser exportada.");
    if (project_root.empty()) throw std::runtime_error("Abra um projeto antes de exportar.");

    namespace fs = std::filesystem;
    const auto root = fs::canonical(project_root);
    const auto parts = original.stem().string() + "_parts";
    const auto requested_directory = root / "assets" / "models" / parts;
    const auto directory = fs::weakly_canonical(requested_directory);
    if (fs::is_symlink(fs::symlink_status(requested_directory)) || !path_inside(directory, root))
        throw std::runtime_error("A pasta de modelos está fora do projeto.");

    const auto destination = directory / (name + ".bmodel");
    const auto materials = directory / (name + ".materials");
    if (fs::exists(fs::symlink_status(destination)))
        throw std::runtime_error("Já existe um arquivo com esse nome na pasta das partes.");
    if (fs::is_symlink(fs::symlink_status(materials)))
        throw std::runtime_error("A pasta de materiais é um link simbólico.");

    commons::editable_model_data exported{{mesh}};
    std::string error;
    if (!exported.validate(error)) throw std::runtime_error(error);
    fs::create_directories(directory);
    auto extracted_material = material;
    for (auto& [slot, texture] : extracted_material.textures) {
        if (texture.embedded || texture.path.empty()) continue;
        fs::create_directories(materials);
        texture.path = import_asset(texture.path, root, false, materials, true).string();
    }
    fs::path material_file;
    if (!commons::store_material_asset(materials, extracted_material, material_file, error))
        throw std::runtime_error(error);
    exported.meshes.front().material_path = material_file.lexically_relative(directory).generic_string();
    static std::atomic_uint64_t next_extract{0};
    fs::path staging;
    while (staging.empty()) {
        const auto candidate = directory / (".bmodel-extract-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(next_extract.fetch_add(1)));
        if (fs::create_directory(candidate)) staging = candidate;
    }
    struct cleanup {
        const fs::path& path;
        ~cleanup() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } guard{staging};

    const auto temporary = staging / destination.filename();
    if (!commons::save_editable_model(temporary, exported, error))
        throw std::runtime_error(error);
    fs::create_hard_link(temporary, destination);
    return destination;
}

}
