#pragma once

#include <filesystem>

namespace editor::project_assets {
    bool package_scene_assets(
        const std::filesystem::path& scene_path,
        const std::filesystem::path& project_root);
}
