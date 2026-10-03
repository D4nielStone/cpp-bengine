#include "system/editor_system.hpp"

#include <algorithm>
#include <vector>

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "elem/menu_bar.hpp"

#include <bgui.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

editor::editor_system::~editor_system() {
    bgui::save_configuration("editor.cfg");
}

void editor::editor_system::setup(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());
    bgui::set_global_scale(0.8f);

    auto& root = bgui::set_layout<bgui::linear>(bgui::orientation::vertical);
    auto& menu_bar = root.add_persistent<bgui::menu_bar>(root);
    auto& config = menu_bar.add_button(" Config ");
    config.add_button("Editor Camera", [this]() {
        open_editor_camera_settings();
    });

    auto& dock = root.add_persistent<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    auto& scene_window = dock.add_window("Editor View Window", bgui::dock_area::center);
    auto& entities_window = dock.add_window("Entities", bgui::dock_area::left);
    auto& components_window = dock.add_window("Components", bgui::dock_area::right);
    auto& assets_window = dock.add_window("Assets Window", bgui::dock_area::right);
    bgui::load_configuration("editor.cfg");

    if (registry) {
        auto editor_camera_entity = registry->create();
        m_editor_camera_entity = editor_camera_entity.id;
        registry->add<COMMONS_NS::camera>(editor_camera_entity);
        if (auto camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity))
            camera_transform->set_rotation(COMMONS_NS::fvec3{0.f, 90.f, 0.f});
        m_camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
        if (auto camera = m_camera.lock())
            camera->createFB();
    }

    setup_scene_view_panel(scene_window);
    setup_entities_panel(entities_window);
    setup_components_panel(components_window);
    setup_assets_panel(assets_window);

    refresh_scene(registry);
    bgui::cascade_style();
    bgui::load_font_queue();
}

void editor::editor_system::update(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry)
        return;

    refresh_scene(registry);
    if (!m_framebuffer_image)
        return;

    if (m_editor_settings) {
        const auto& elements = bgui::get_layout().get_elements();
        const auto overlays = elements.find(bgui::layer::overlay);
        const bool settings_attached = overlays != elements.end() &&
            std::any_of(overlays->second.begin(), overlays->second.end(), [this](const auto& element) {
                return element.get() == m_editor_settings;
            });
        if (!settings_attached)
            m_editor_settings = nullptr;
    }

    if (m_camera.expired()) {
        registry->cada<COMMONS_NS::camera>([&](const uint32_t entity) {
            if (entity == m_editor_camera_entity || !m_camera.expired())
                return;
            if (auto camera = registry->get<COMMONS_NS::camera>(entity))
                m_camera = camera;
        });
    }

    update_scene_view_panel(registry);
}

void editor::editor_system::refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry || !m_entities_list || !m_components_list)
        return;

    std::vector<std::pair<uint32_t, uint32_t>> signature;
    signature.reserve(registry->entities.size());
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
        if (entity_id == m_editor_camera_entity)
            continue;
        signature.emplace_back(entity_id, static_cast<uint32_t>(registry->get_components(entity_id)));
    }
    if (m_scene_initialized && signature == m_scene_signature)
        return;

    m_scene_signature = std::move(signature);
    m_scene_initialized = true;
    const bool selected_exists = std::any_of(
        m_scene_signature.begin(), m_scene_signature.end(),
        [this](const auto& entry) { return entry.first == m_selected_entity; }
    );
    if (!selected_exists)
        m_selected_entity = m_scene_signature.empty() ? 0 : m_scene_signature.front().first;

    rebuild_entities(registry);
    rebuild_components(registry);
}