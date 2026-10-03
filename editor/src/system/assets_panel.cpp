#include "system/editor_system.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <bgui.hpp>
#include <elem/details.hpp>

namespace {
    void add_asset_group(
        bgui::linear& context,
        const std::string& title,
        const std::filesystem::path& directory)
    {
        auto& section = context.add_persistent<bgui::details>(title);
        std::vector<std::filesystem::path> files;
        std::error_code error;
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            if (it->is_regular_file(error))
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            auto& item = section.content().add_persistent<bgui::text>(file.filename().string(), 0.35f);
            item.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
    }
}

void editor::editor_system::setup_assets_panel(bgui::window& window) {
    auto& context = window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    add_asset_group(context, "Models", COMMONS_MODEL_ASSET_DIR);
    add_asset_group(context, "Shaders", COMMONS_SHADER_ASSET_DIR);
}