#pragma once

#include <filesystem>
#include <string>

namespace editor::ui {

enum class project_template {
    empty,
    fpp
};

std::filesystem::path project_file_path(const std::filesystem::path& project_folder);
bool create_project_files(
    const std::filesystem::path& project_folder,
    std::string& error,
    project_template selected_template = project_template::empty
);

}
