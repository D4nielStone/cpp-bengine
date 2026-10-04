#include "system/editor_system.hpp"
#include "system/editor_ui_elements.hpp"

#include <bgui.hpp>
#include <elem/button.hpp>

namespace {
    void clear_layout(bgui::linear& layout) {
        for (auto& layer_elements : layout.get_elements())
            layer_elements.second.clear();
    }
}

void editor::editor_system::setup_entities_panel(bgui::window& window) {
    auto& context = window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    context.style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::start,
        bgui::alignment::center
    };
    auto& title = context.add_persistent<bgui::text>("Current Scene", 0.4f);
    title.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
    auto& entities_list = context.add_persistent<bgui::linear>(bgui::orientation::vertical);
    entities_list.add_class(editor::ui_elements::entities_list);
    entities_list.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
}

void editor::editor_system::select_entity(
    const uint32_t entity_id,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || registry->entities.find(entity_id) == registry->entities.end())
        return;

    m_selected_entity = entity_id;
    rebuild_entities(registry);
    rebuild_components(registry);
}

void editor::editor_system::rebuild_entities(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    auto& entities_list = editor::ui_elements::require<bgui::linear>(editor::ui_elements::entities_list);
    clear_layout(entities_list);
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = registry;
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
        if (entity_id == m_editor_camera_entity)
            continue;
        const std::string label = (entity_id == m_selected_entity ? "* Entity " : "  Entity ") +
            std::to_string(entity_id);
        auto& row = entities_list.add_persistent<bgui::button>(label, 0.35f, [this, weak_registry, entity_id]() {
            if (const auto current_registry = weak_registry.lock())
                select_entity(entity_id, current_registry);
        });
        row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}