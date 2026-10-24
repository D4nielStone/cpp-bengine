#pragma once

#include <components/camera.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace editor::ui {

std::filesystem::path project_preview_path(const std::string& project_path);
bool save_project_preview(
    const std::string& project_path,
    const std::shared_ptr<commons::camera>& camera,
    std::string& error
);

}
