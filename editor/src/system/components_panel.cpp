#include "system/editor_system.hpp"
#include "system/editor_ui_elements.hpp"

#include <array>

#include "core/ecs.hpp"

#include <bgui.hpp>

namespace {
    void clear_layout(bgui::linear& layout) {
        for (auto& layer_elements : layout.get_elements())
            layer_elements.second.clear();
    }
}

void editor::editor_system::setup_components_panel(bgui::window& window) {
    auto& context = window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    auto& components_list = context.add_persistent<bgui::linear>(bgui::orientation::vertical);
    components_list.add_class(editor::ui_elements::components_list);
    components_list.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
}

void editor::editor_system::rebuild_components(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    auto& components_list = editor::ui_elements::require<bgui::linear>(editor::ui_elements::components_list);
    clear_layout(components_list);
    if (m_selected_entity == 0) {
        auto& empty = components_list.add_persistent<bgui::text>("No entity selected", 0.35f);
        empty.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        return;
    }

    static constexpr std::array component_names{
        std::pair{COMMONS_NS::component::COMPONENTE_TRANSFORMACAO, "Transform"},
        std::pair{COMMONS_NS::component::COMPONENTE_CAM, "Camera"},
        std::pair{COMMONS_NS::component::COMPONENTE_RENDER, "Renderer"},
        std::pair{COMMONS_NS::component::COMPONENTE_PROPRIEDADES, "Properties"},
        std::pair{COMMONS_NS::component::COMPONENTE_TEXTO, "Text"},
        std::pair{COMMONS_NS::component::COMPONENTE_CODIGO, "Code"},
        std::pair{COMMONS_NS::component::COMPONENTE_IMAGEM, "Image"},
        std::pair{COMMONS_NS::component::COMPONENTE_FISICA, "Physics"},
        std::pair{COMMONS_NS::component::COMPONENTE_LUZ_PONTUAL, "Point Light"},
        std::pair{COMMONS_NS::component::COMPONENTE_LUZ_DIRECIONAL, "Directional Light"},
        std::pair{COMMONS_NS::component::COMPONENTE_LUZ_HOLOFOTE, "Spot Light"},
        std::pair{COMMONS_NS::component::COMPONENTE_TERRENO, "Terrain"}
    };
    const auto mask = static_cast<uint32_t>(registry->get_components(m_selected_entity));
    for (const auto& [component_mask, name] : component_names) {
        if ((mask & static_cast<uint32_t>(component_mask)) == 0)
            continue;
        auto& row = components_list.add_persistent<bgui::text>(name, 0.35f);
        row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}