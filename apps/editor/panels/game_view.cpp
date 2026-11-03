#define GLFW_INCLUDE_NONE
#include <glad.h>

#include "game_view.hpp"

#include "components/camera.hpp"

#include <elem/image.hpp>
#include <elem/text.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace editor::ui {

struct game_view_state {
    std::shared_ptr<editor_context> context;
    bgui::window* window{nullptr};
    bgui::image* image{nullptr};
    bgui::text* status{nullptr};
    std::shared_ptr<commons::camera> camera;
    std::uint32_t camera_entity{0};
    unsigned int displayed_texture{0};
};

namespace {

std::shared_ptr<commons::camera> active_game_camera(
    const std::shared_ptr<editor_context>& context,
    std::uint32_t& camera_entity
) {
    std::shared_ptr<commons::camera> camera;
    context->registry->cada<commons::camera>([&](const std::uint32_t id) {
        if (camera || (context->editor_camera_entity &&
                       id == *context->editor_camera_entity))
            return;
        auto candidate = context->registry->get<commons::camera>(id);
        if (candidate) {
            camera_entity = id;
            camera = std::move(candidate);
        }
    });

    if (camera)
        return camera;
    if (!context->editor_camera_entity)
        return {};

    camera_entity = *context->editor_camera_entity;
    return context->registry->get<commons::camera>(camera_entity);
}

void update_game_view(const std::shared_ptr<game_view_state>& state) {
    if (!state || !state->context || !state->window ||
        !state->image || !state->status)
        return;

    if (!state->window->is_enabled()) return;

    std::uint32_t camera_entity = 0;
    auto camera = active_game_camera(state->context, camera_entity);
    if (!camera) {
        state->camera.reset();
        state->camera_entity = 0;
        state->displayed_texture = 0;
        state->image->set_enable(false);
        state->status->set_buffer("No game camera. HUD: no game HUD system.");
        return;
    }

    state->camera = std::move(camera);
    state->camera_entity = camera_entity;
    state->image->set_enable(true);
    const int width = state->image->processed_width();
    const int height = state->image->processed_height();
    if (width > 0 && height > 0 &&
        (!state->camera->flag_fb ||
         state->camera->viewportFBO.x != width ||
         state->camera->viewportFBO.y != height))
        state->camera->request_framebuffer_size({width, height});

    const auto texture = state->camera->framebuffer_texture();
    if (texture != 0 && texture != state->displayed_texture) {
        state->image->set_external_texture(
            texture,
            bgui::vec2{
                static_cast<float>(state->camera->viewportFBO.x),
                static_cast<float>(state->camera->viewportFBO.y)
            }
        );
        state->displayed_texture = texture;
    }

    const auto name = state->context->entity_names.find(camera_entity);
    const std::string camera_name = name != state->context->entity_names.end()
        ? name->second
        : "Camera " + std::to_string(camera_entity);
    state->status->set_buffer(camera_name + " | HUD: no game HUD system.");
}

class game_view_driver final : public bgui::element {
public:
    explicit game_view_driver(std::shared_ptr<game_view_state> state)
        : m_state(std::move(state)) {
        receives_input(false);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        update_game_view(m_state);
    }

private:
    std::shared_ptr<game_view_state> m_state;
};

}

game_view::game_view(std::shared_ptr<editor_context> context)
    : m_state(std::make_shared<game_view_state>()) {
    if (!context || !context->registry)
        throw std::invalid_argument("game_view requires an editor context");
    m_state->context = std::move(context);
}

void game_view::build(bgui::dock& dock) {
    auto& window = dock.add_window("Game", bgui::dock_area::center, 0.3f);
    window.add_class("editor-panel");
    m_state->window = &window;

    auto& content = window.add<bgui::linear>(bgui::orientation::vertical);
    content.add_class("editor-surface");
    content.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );
    content.style.layout.padding = bgui::vec4i{0};

    auto& image = content.add<bgui::image>();
    image.add_class("editor-viewport-surface");
    image.receives_input(false);
    image.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::stretch
    );
    m_state->image = &image;
    m_state->status = &content.add<bgui::text>(
        "Waiting for a game camera. HUD: no game HUD system.",
        0.27f
    );
    m_state->status->add_class("project-manager-muted");

    bgui::get_layout().add<game_view_driver, bgui::layer::overlay>(m_state);
    update_game_view(m_state);
}

void game_view::update() {
    update_game_view(m_state);
}

bool game_view::needs_camera_render(const std::uint32_t entity) const noexcept {
    return m_state && m_state->context &&
        m_state->window && m_state->window->is_enabled() &&
        m_state->camera && m_state->camera_entity == entity;
}

}
