#include "system/editor_system.hpp"
#include "system/editor_ui_elements.hpp"

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

void editor::editor_system::setup_assets_panel(
    bgui::window& window,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    auto& context = window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    m_registry = registry;
    auto& project_status = context.add_persistent<bgui::text>("Nenhum projeto aberto", 0.32f);
    project_status.add_class(editor::ui_elements::project_status);
    project_status.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = registry;
    auto& import_model = context.add_persistent<bgui::button>("Importar modelo 3D", 0.35f, [this, weak_registry]() {
        browse_model_file(weak_registry.lock());
    });
    import_model.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& scenes = context.add_persistent<bgui::details>("Scenes");
    auto& project_scenes_list = scenes.content().add_persistent<bgui::linear>(bgui::orientation::vertical);
    project_scenes_list.add_class(editor::ui_elements::project_scenes_list);
    project_scenes_list.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& model_import_status = context.add_persistent<bgui::text>("", 0.32f);
    model_import_status.add_class(editor::ui_elements::model_import_status);
    model_import_status.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    refresh_project_scenes();
}