#include "editor/editor_system.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "components/camera.hpp"

#include <bgui.hpp>
#include <bgui_backend_glfw.hpp>
#include <elem/details.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

namespace {
    void clear_layout(bgui::linear& layout) {
        for (auto& layer_elements : layout.get_elements())
            layer_elements.second.clear();
    }

    void add_asset_group(bgui::linear& window, const std::string& title,
                         const std::filesystem::path& directory) {
        auto& section = window.add_persistent<bgui::details>(title);
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

void editor::editor_system::setup(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    bgui::style_manager::get_instance().apply_theme(bgui::dark_theme());

    auto& root = bgui::get_layout();
    auto& dock = root.add_persistent<bgui::dock>();
    auto& window = dock.add_window("Main Window", bgui::dock_area::center);
    auto& entities_window = dock.add_window("Entities", bgui::dock_area::left);
    auto& components_window = dock.add_window("Components", bgui::dock_area::right);
    auto& window_assets = dock.add_window("Assets Window", bgui::dock_area::right);

    auto& entities_context = entities_window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    entities_context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    entities_context.style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::start,
        bgui::alignment::center
    };
    auto& scene_title = entities_context.add_persistent<bgui::text>("Cena atual", 0.4f);
    scene_title.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
    m_entities_list = &entities_context.add_persistent<bgui::linear>(bgui::orientation::vertical);
    m_entities_list->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);

    auto& components_context = components_window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    components_context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    m_components_list = &components_context.add_persistent<bgui::linear>(bgui::orientation::vertical);
    m_components_list->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);

    auto& assets_context = window_assets.add_persistent<bgui::linear>(bgui::orientation::vertical);
    assets_context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    window.style.layout.padding = bgui::vec4i{0};
    add_asset_group(assets_context, "Models", COMMONS_MODEL_ASSET_DIR);
    add_asset_group(assets_context, "Shaders", COMMONS_SHADER_ASSET_DIR);

    m_window_context = &window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    m_window_context->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    m_window_context->style.layout.padding = bgui::vec4i{0};
    m_window_context->style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::center,
        bgui::alignment::center
    };
    m_framebuffer_image = &m_window_context->add_persistent<bgui::image>();
    m_framebuffer_image->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);

    refresh_scene(registry);
    bgui::cascade_style();
    bgui::load_font_queue();
}

void editor::editor_system::update(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry)
        return;

    refresh_scene(registry);
    if (!m_framebuffer_image)
        return;

    if (m_camera.expired()) {
        registry->cada<COMMONS_NS::camera>([&](const uint32_t entity) {
            if (!m_camera.expired())
                return;

            auto camera_component = registry->get<COMMONS_NS::camera>(entity);
            if (!camera_component)
                return;

            if (!camera_component->flag_fb)
                camera_component->createFB();

            m_framebuffer_image->set_external_texture(
                camera_component->framebuffer_texture(),
                bgui::vec2{
                    static_cast<float>(camera_component->viewportFBO.x),
                    static_cast<float>(camera_component->viewportFBO.y)
                }
            );
            m_camera = camera_component;
        });
    }

    const auto camera_component = m_camera.lock();
    if (!camera_component)
        return;

    const auto source_size = camera_component->viewportFBO;
    const auto padding = m_window_context->computed_style.layout.padding;
    const int available_width = std::max(0, m_window_context->processed_width() - padding.x - padding.z);
    const int available_height = std::max(0, m_window_context->processed_height() - padding.y - padding.w);
    if (source_size.x <= 0 || source_size.y <= 0 || available_width <= 0 || available_height <= 0)
        return;

    const float source_aspect = static_cast<float>(source_size.x) / source_size.y;
    const float target_aspect = static_cast<float>(available_width) / available_height;
    bgui::vec2 uv_min{0.f, 0.f};
    bgui::vec2 uv_max{1.f, 1.f};
    if (source_aspect > target_aspect) {
        const float horizontal_crop = (1.f - target_aspect / source_aspect) * 0.5f;
        uv_min[0] = horizontal_crop;
        uv_max[0] = 1.f - horizontal_crop;
    } else if (source_aspect < target_aspect) {
        const float vertical_crop = (1.f - source_aspect / target_aspect) * 0.5f;
        uv_min[1] = vertical_crop;
        uv_max[1] = 1.f - vertical_crop;
    }
    m_framebuffer_image->set_uv_region(uv_min, uv_max);

    if (available_width != m_framebuffer_display_width || available_height != m_framebuffer_display_height) {
        m_framebuffer_image->set_size(static_cast<float>(available_width), static_cast<float>(available_height));
        m_framebuffer_display_width = available_width;
        m_framebuffer_display_height = available_height;
    }
}

void editor::editor_system::refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || !m_entities_list || !m_components_list)
        return;

    std::vector<std::pair<uint32_t, uint32_t>> signature;
    signature.reserve(registry->entities.size());
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
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

void editor::editor_system::rebuild_entities(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    clear_layout(*m_entities_list);
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = registry;
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
        const std::string label = (entity_id == m_selected_entity ? "> Entity " : "Entity ") +
            std::to_string(entity_id);
        auto& row = m_entities_list->add_persistent<bgui::button>(label, 0.35f, [this, weak_registry, entity_id]() {
            if (const auto current_registry = weak_registry.lock())
                select_entity(entity_id, current_registry);
        });
        row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}

void editor::editor_system::rebuild_components(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    clear_layout(*m_components_list);
    if (m_selected_entity == 0) {
        auto& empty = m_components_list->add_persistent<bgui::text>("No entity selected", 0.35f);
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
        auto& row = m_components_list->add_persistent<bgui::text>(name, 0.35f);
        row.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}