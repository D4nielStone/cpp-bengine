#include "system/editor_system.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "elem/menu_bar.hpp"

#include <bgui.hpp>
#include <bgui_backend_glfw.hpp>
#include <elem/details.hpp>
#include <elem/input_area.hpp>
#include <os/os.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

namespace {
    float setting_value(const std::string& text, const float current,
                        const float minimum, const float maximum) {
        try {
            return std::clamp(std::stof(text), minimum, maximum);
        } catch (...) {
            return current;
        }
    }

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
void editor::editor_system::setup(
    const std::shared_ptr<COMMONS_NS::ecs>& registry
) {
    bgui::style_manager::get_instance().apply_theme(
        bgui::dark_theme()
    );

    bgui::set_global_scale(0.8);

    auto& root =
        bgui::set_layout<bgui::linear>(
            bgui::orientation::vertical
        );

    auto& menu_bar =
        root.add_persistent<bgui::menu_bar>(root);

    auto& config =
        menu_bar.add_button(" Config ");

    config.add_button(
        "Editor Camera",
        [this]() {
            open_editor_camera_settings();
        }
    );

    auto& dock =
        root.add_persistent<bgui::dock>();

    dock.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::stretch
    );

    auto& window = dock.add_window("Editor View Window", bgui::dock_area::center);
    auto& entities_window = dock.add_window("Entities", bgui::dock_area::left);
    auto& components_window = dock.add_window("Components", bgui::dock_area::right);
    auto& window_assets = dock.add_window("Assets Window", bgui::dock_area::right);

    if (registry) {
        auto editor_camera_entity = registry->create();
        m_editor_camera_entity = editor_camera_entity.id;
        registry->add<COMMONS_NS::camera>(editor_camera_entity);
        if (auto editor_camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity))
            editor_camera_transform->set_rotation(COMMONS_NS::fvec3{0.f, 90.f, 0.f});
        m_camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
        if (auto editor_camera = m_camera.lock())
            editor_camera->createFB();
    }

    auto& entities_context = entities_window.add_persistent<bgui::linear>(bgui::orientation::vertical);
    entities_context.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    entities_context.style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::start,
        bgui::alignment::center
    };
    auto& scene_title = entities_context.add_persistent<bgui::text>("Current Scene", 0.4f);
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

void editor::editor_system::open_editor_camera_settings() {
    if (!m_editor_settings)
        create_editor_camera_settings();

    const auto size = bgui::get_context_size();

    m_editor_settings->set_position(
        std::max(
            0,
            (size.x - m_editor_settings->processed_width()) / 2
        ),
        std::max(
            0,
            (size.y - m_editor_settings->processed_height()) / 2
        )
    );

    m_editor_settings->set_enable(true);
    m_editor_settings->set_flex(false);
}
void editor::editor_system::create_editor_camera_settings() {
    auto& root = bgui::get_layout();

    auto& settings_window =
        root.add_persistent<
            bgui::window,
            bgui::layer::overlay
        >("Editor Camera");

    m_editor_settings = &settings_window;

    auto add_setting =
        [this, &settings_window](
            const std::string& title,
            const std::string& initial,
            const std::function<void(const std::string)>& apply
        ) {
            auto& row =
                settings_window.add_persistent<bgui::linear>(
                    bgui::orientation::horizontal
                );

            row.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );

            auto& label =
                row.add_persistent<bgui::text>(
                    title,
                    0.3f
                );

            label.style.layout.require_mode(
                bgui::mode::stretch,
                bgui::mode::match_parent
            );

            auto& input =
                row.add_persistent<bgui::input_area>(
                    initial,
                    0.3f,
                    apply
                );

            input.style.layout.require_mode(
                bgui::mode::pixel,
                bgui::mode::match_parent
            );

            input.style.layout.require_size(
                100.f,
                30.f
            );
        };

    add_setting(
        "Move speed",
        "4.0",
        [this](const std::string value) {
            m_camera_move_speed =
                setting_value(
                    value,
                    m_camera_move_speed,
                    0.1f,
                    100.f
                );
        }
    );

    add_setting(
        "Look sensitivity",
        "0.12",
        [this](const std::string value) {
            m_camera_look_sensitivity =
                setting_value(
                    value,
                    m_camera_look_sensitivity,
                    0.01f,
                    2.f
                );
        }
    );

    add_setting(
        "Zoom sensitivity",
        "3.0",
        [this](const std::string value) {
            m_camera_zoom_sensitivity =
                setting_value(
                    value,
                    m_camera_zoom_sensitivity,
                    0.1f,
                    20.f
                );
        }
    );

    add_setting(
        "UI scale",
        "0.7",
        [this](const std::string value) {
            m_ui_scale =
                setting_value(
                    value,
                    m_ui_scale,
                    0.5f,
                    2.f
                );

            bgui::set_global_scale(m_ui_scale);
        }
    );
}
void editor::editor_system::update(const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry)
        return;

    refresh_scene(registry);
    if (!m_framebuffer_image)
        return;

    const float ui_scale = bgui::get_global_scale();
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
            if (entity == m_editor_camera_entity)
                return;
            if (!m_camera.expired())
                return;

            auto camera_component = registry->get<COMMONS_NS::camera>(entity);
            if (!camera_component)
                return;

            m_camera = camera_component;
        });
    }

    const auto camera_component = m_camera.lock();
    if (!camera_component)
        return;
    if (!camera_component->flag_fb)
        camera_component->createFB();
    if (m_framebuffer_texture != camera_component->framebuffer_texture()) {
        m_framebuffer_texture = camera_component->framebuffer_texture();
        m_framebuffer_image->set_external_texture(
            m_framebuffer_texture,
            bgui::vec2{
                static_cast<float>(camera_component->viewportFBO.x),
                static_cast<float>(camera_component->viewportFBO.y)
            }
        );
    }

    const auto mouse_position = bgui::get_mouse_position();
    const auto image_rect = m_framebuffer_image->processed_rect();
    const bool mouse_inside_view = mouse_position.x >= image_rect.x &&
        mouse_position.x <= image_rect.x + image_rect.z &&
        mouse_position.y >= image_rect.y &&
        mouse_position.y <= image_rect.y + image_rect.w;
    const float scroll_delta = bgui::get_context().m_scroll_delta_y;
    bgui::get_context().m_scroll_delta_y = 0.f;
    const auto& inputs = bgui::get_context().m_input_map;
    const auto input_down = [&inputs](const bgui::input_key key) {
        const auto found = inputs.find(key);
        return found != inputs.end() &&
            (found->second == bgui::input_action::press || found->second == bgui::input_action::repeat);
    };
    const bool left_down = input_down(bgui::input_key::mouse_left);
    const bool right_down = input_down(bgui::input_key::mouse_right);
    const bool left_just_pressed = left_down && !m_left_mouse_was_down;
    const bool right_just_pressed = right_down && !m_right_mouse_was_down;
    if (left_just_pressed && mouse_inside_view)
        m_left_view_active = true;
    if (right_just_pressed && mouse_inside_view)
        m_right_move_active = true;
    if (!left_down)
        m_left_view_active = false;
    if (!right_down)
        m_right_move_active = false;
    m_left_mouse_was_down = left_down;
    m_right_mouse_was_down = right_down;

    const float current_time = bgui::get_time();
    const float delta_time = m_last_update_time > 0.f
        ? std::min(current_time - m_last_update_time, 0.1f)
        : 0.f;
    m_last_update_time = current_time;
    const int mouse_delta_x = mouse_position.x - m_last_mouse_x;
    const int mouse_delta_y = mouse_position.y - m_last_mouse_y;
    m_last_mouse_x = mouse_position.x;
    m_last_mouse_y = mouse_position.y;

    auto camera_transform = registry->get<COMMONS_NS::transform>(camera_component->my_object);
    if (camera_transform && mouse_inside_view && scroll_delta != 0.f) {
        const auto rotation = camera_transform->get_rotation();
        const float pitch = glm::radians(rotation.x);
        const float yaw = glm::radians(rotation.y);
        const float distance = scroll_delta * m_camera_zoom_sensitivity;
        camera_transform->move({
            std::cos(yaw) * std::cos(pitch) * distance,
            std::sin(pitch) * distance,
            std::sin(yaw) * std::cos(pitch) * distance
        });
    }
    if (camera_transform && delta_time > 0.f) {
        auto rotation = camera_transform->get_rotation();
        if (m_left_view_active) {
            const int view_delta_x = left_just_pressed ? 0 : mouse_delta_x;
            const int view_delta_y = left_just_pressed ? 0 : mouse_delta_y;
            rotation.x = std::clamp(rotation.x - view_delta_y * m_camera_look_sensitivity, -89.f, 89.f);
            rotation.y += view_delta_x * m_camera_look_sensitivity;
            camera_transform->set_rotation(rotation);
        }

        if (m_right_move_active) {
            const float pitch = glm::radians(rotation.x);
            const float yaw = glm::radians(rotation.y);
            const COMMONS_NS::fvec3 forward{
                std::cos(yaw) * std::cos(pitch),
                std::sin(pitch),
                std::sin(yaw) * std::cos(pitch)
            };
            const COMMONS_NS::fvec3 right{-std::sin(yaw), 0.f, std::cos(yaw)};
            float move_x = 0.f;
            float move_y = 0.f;
            float move_z = 0.f;
            if (input_down(bgui::input_key::w)) { move_x += forward.x; move_y += forward.y; move_z += forward.z; }
            if (input_down(bgui::input_key::s)) { move_x -= forward.x; move_y -= forward.y; move_z -= forward.z; }
            if (input_down(bgui::input_key::d)) { move_x += right.x; move_z += right.z; }
            if (input_down(bgui::input_key::a)) { move_x -= right.x; move_z -= right.z; }
            if (input_down(bgui::input_key::e)) move_y += 1.f;
            if (input_down(bgui::input_key::q)) move_y -= 1.f;

            const float move_length = std::sqrt(move_x * move_x + move_y * move_y + move_z * move_z);
            auto position = camera_transform->get_position();
            if (move_length > 0.f) {
                const float speed = m_camera_move_speed * (input_down(bgui::input_key::left_shift) ||
                    input_down(bgui::input_key::right_shift) ? 3.f : 1.f);
                const float distance = speed * delta_time / move_length;
                position.x += move_x * distance;
                position.y += move_y * distance;
                position.z += move_z * distance;
            }
            if (!right_just_pressed) {
                position.x -= right.x * mouse_delta_x * 0.02f;
                position.y += mouse_delta_y * 0.02f;
                position.z -= right.z * mouse_delta_x * 0.02f;
            }
            camera_transform->set_position(position);
        }
    }

    const auto source_size = camera_component->viewportFBO;
    const auto padding = m_window_context->computed_style.layout.padding;
    const int available_width = std::max(0, m_window_context->processed_width()                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  - padding.x - padding.z);
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
        if (entity_id == m_editor_camera_entity)
            continue;
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