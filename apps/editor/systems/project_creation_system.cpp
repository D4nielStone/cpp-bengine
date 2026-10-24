#include "project_creation_system.hpp"

#include "components/camera.hpp"
#include "core/ecs.hpp"
#include "project_persistence_system.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace editor::ui {

std::filesystem::path project_file_path(
    const std::filesystem::path& project_folder
) {
    return project_folder / "project.bengine";
}

bool create_project_files(
    const std::filesystem::path& project_folder,
    std::string& error
) {
    error.clear();
    if (project_folder.empty() || project_folder.filename().empty() ||
        project_folder.filename() == "." || project_folder.filename() == "..") {
        error = "Choose a valid project folder name.";
        return false;
    }

    std::error_code filesystem_error;
    const auto absolute_folder = std::filesystem::absolute(
        project_folder,
        filesystem_error
    ).lexically_normal();
    if (filesystem_error) {
        error = "Could not resolve project location: " +
            filesystem_error.message();
        return false;
    }
    if (std::filesystem::exists(absolute_folder, filesystem_error)) {
        if (filesystem_error) {
            error = "Could not inspect project location: " +
                filesystem_error.message();
            return false;
        }
        error = "A file or folder already exists at '" +
            absolute_folder.string() + "'. Choose a different project name.";
        return false;
    }
    if (filesystem_error) {
        error = "Could not inspect project location: " +
            filesystem_error.message();
        return false;
    }

    std::filesystem::create_directories(
        absolute_folder / "assets" / "models",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }
    std::filesystem::create_directories(
        absolute_folder / "assets" / "textures",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }
    std::filesystem::create_directories(
        absolute_folder / "scripts",
        filesystem_error
    );
    if (filesystem_error) {
        error = "Could not create project folders: " +
            filesystem_error.message();
        return false;
    }

    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    project_persistence_system persistence(registry, camera_entity.id);
    return persistence.save(project_file_path(absolute_folder).string(), error);
}

}
