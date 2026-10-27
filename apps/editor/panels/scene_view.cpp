#include "scene_view.hpp"

#include "camera_movement.hpp"
#include "entity_picking_system.hpp"
#include "guizmo_system.hpp"
#include "viewport_sync.hpp"
#include "camera_preview_state.hpp"

#include <bgui.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <algorithm>

#include <stdexcept>
#include <utility>

#include "viewport_input_state.hpp"

namespace editor::ui {

struct scene_view_state {
    bgui::image* image{nullptr};
};

namespace {

class preview_card final : public bgui::linear {
public:
    preview_card() : bgui::linear(bgui::orientation::vertical) {}
    bool clips_children() const override { return true; }
};

class navigation_hint final : public bgui::text {
public:
    explicit navigation_hint(std::shared_ptr<scene_view_state> state)
        : bgui::text("Space + drag: look    Shift + Space + drag: pan\n"
                     "Scroll: dolly    Space + WASD / QE: move", 0.25f),
          m_state(std::move(state)) {
        receives_input(false);
        set_flex(false);
        set_wrap(false);
    }
    void on_update() override {
        if (!m_state->image)
            return;
        const auto rect = m_state->image->processed_rect();
        set_final_rect(rect.x + 8, rect.y + std::max(0, rect.w - 32),
            std::max(0, rect.z - 16), 28);
    }
private:
    std::shared_ptr<scene_view_state> m_state;
};

class camera_preview_overlay final : public bgui::element {
public:
    camera_preview_overlay(bgui::linear& frame, bgui::text& title, bgui::image& image,
        std::shared_ptr<camera_preview_state> state, std::shared_ptr<editor_context> context)
        : m_frame(&frame), m_title(&title), m_image(&image),
          m_state(std::move(state)), m_context(std::move(context)) {
        receives_input(false);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        if (!m_image || !m_state || !m_state->hovered ||
            !m_state->main_runtime_camera) {
            m_frame->set_enable(false);
            return;
        }

        const float ui_scale = bgui::get_global_scale();
        const int margin = static_cast<int>(12.f * ui_scale);
        const int padding = static_cast<int>(6.f * ui_scale);
        const int header = static_cast<int>(18.f * ui_scale);
        const auto& viewport = m_state->viewport;
        const float available_width = viewport.z - 2 * (margin + padding);
        const float available_height = viewport.w - 2 * (margin + padding) - header;
        const float scale = std::min({1.f, available_width / (240.f * ui_scale),
            available_height / (150.f * ui_scale)});
        if (scale <= 0.f) {
            m_frame->set_enable(false);
            return;
        }
        const int preview_width = std::max(1, static_cast<int>(240 * ui_scale * scale));
        const int preview_height = std::max(1, static_cast<int>(150 * ui_scale * scale));
        auto& camera = m_state->main_runtime_camera;
        if (!camera->flag_fb) {
            camera->createFB();
            camera->viewport({preview_width, preview_height});
        } else if (camera->viewportFBO.x != preview_width ||
                   camera->viewportFBO.y != preview_height) {
            camera->viewport({preview_width, preview_height});
        }
        if (camera->framebuffer_texture() == 0) {
            m_frame->set_enable(false);
            return;
        }

        const int frame_width = preview_width + 2 * padding;
        const int frame_height = preview_height + 2 * padding + header;
        const int preview_x = std::max(
            viewport.x + margin,
            viewport.x + viewport.z - frame_width - margin
        );
        const auto name = m_context->entity_names.find(camera->my_object);
        m_title->set_buffer(name != m_context->entity_names.end()
            ? name->second : "Camera " + std::to_string(camera->my_object));
        m_image->set_external_texture(
            camera->framebuffer_texture(),
            bgui::vec2{
                static_cast<float>(camera->viewportFBO.x),
                static_cast<float>(camera->viewportFBO.y)
            }
        );
        m_frame->set_final_rect(preview_x, viewport.y + margin, frame_width, frame_height);
        m_image->set_size(preview_width / ui_scale, preview_height / ui_scale);
        m_frame->set_enable(true);
    }

private:
    bgui::linear* m_frame;
    bgui::text* m_title;
    bgui::image* m_image;
    std::shared_ptr<camera_preview_state> m_state;
    std::shared_ptr<editor_context> m_context;
};

class scene_view_surface final : public bgui::linear {
public:
    explicit scene_view_surface(std::shared_ptr<scene_view_state> state)
        : bgui::linear(bgui::orientation::vertical),
          m_state(std::move(state)) {
    }

    ~scene_view_surface() override {
        m_state->image = nullptr;
    }

private:
    std::shared_ptr<scene_view_state> m_state;
};

class scene_view_runtime final : public bgui::element {
public:
    scene_view_runtime(
        std::shared_ptr<commons::ecs> registry,
        std::shared_ptr<commons::camera> camera,
        std::shared_ptr<editor_context> context,
        std::shared_ptr<viewport_input_state> input_state,
        const std::shared_ptr<scene_view_state>& state
    )
        : m_registry(std::move(registry)),
          m_viewport_sync(camera, *state->image),
          m_camera_movement(camera, *state->image, input_state),
          m_entity_picking(
              std::move(camera),
              *state->image,
              std::move(context),
              std::move(input_state)
          ) {
        set_flex(false);
        // This overlay spans the scene window. It acts as the scene viewport's
        // input hit target so hit testing can consume input without hiding the
        // owning window from viewport_input_state.
        receives_input(true);
        style.visual.visible = false;
        m_camera_movement.setup(m_registry);
        m_entity_picking.setup(m_registry);
    }

    void on_update() override {
        m_viewport_sync.update(m_registry);
        m_camera_movement.update(m_registry);
        m_entity_picking.update(m_registry);
    }

private:
    std::shared_ptr<commons::ecs> m_registry;
    viewport_sync_system m_viewport_sync;
    camera_movement_system m_camera_movement;
    entity_picking_system m_entity_picking;
};

}

scene_view::scene_view(
    commons::window& window,
    std::shared_ptr<commons::camera> camera,
    std::shared_ptr<editor_context> context
)
    : m_window(window),
      m_camera(camera),
      m_context(std::move(context)),
      m_input_state(std::make_shared<viewport_input_state>()),
      m_camera_preview(std::make_shared<camera_preview_state>()),
      m_state(std::make_shared<scene_view_state>()) {
    if (!m_camera)
        throw std::invalid_argument("scene_view requires a camera");
}

void scene_view::build(bgui::dock& dock) {
    if (!m_camera->flag_fb)
        m_camera->createFB();

    auto& scene_window = dock.add_window("Scene", bgui::dock_area::center, 0.3f);
    scene_window.add_class("editor-panel");

    auto& scene_context = scene_window.add<scene_view_surface>(m_state);
    scene_context.add_class("editor-surface");
    scene_context.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    scene_context.style.layout.padding = bgui::vec4i{0};

    auto& image = scene_context.add<bgui::image>();
    m_state->image = &image;
    image.add_class("editor-viewport-surface");

    image.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::match_parent
    );

    image.receives_input(true);

    image.set_external_texture(
        m_camera->framebuffer_texture(),
        bgui::vec2{
            static_cast<float>(m_camera->viewportFBO.x),
            static_cast<float>(m_camera->viewportFBO.y)
        }
    );

    scene_window.add<scene_view_runtime, bgui::layer::overlay>(
        m_window.get_ecs(),
        m_camera,
        m_context,
        m_input_state,
        m_state
    );

    scene_window.add<guizmo_system, bgui::layer::overlay>(
        m_camera,
        image,
        m_context,
        m_input_state,
        m_camera_preview
    );

    auto& preview_frame = scene_window.add<preview_card, bgui::layer::overlay>();
    preview_frame.add_class("camera-preview-card");
    preview_frame.receives_input(false);
    preview_frame.set_flex(false);
    preview_frame.set_enable(false);
    auto& preview_header = preview_frame.add<bgui::linear>(bgui::orientation::horizontal);
    preview_header.style.layout.require_height(bgui::mode::pixel, 18.f);
    preview_header.style.layout.require_width(bgui::mode::match_parent);
    preview_header.style.layout.padding = bgui::vec4i{0};
    preview_header.receives_input(false);
    preview_header.add<bgui::icon>("camera", 0.24f).receives_input(false);
    auto& preview_title = preview_header.add<bgui::text>("Camera Preview", 0.27f);
    preview_title.receives_input(false);
    preview_title.style.layout.require_width(bgui::mode::stretch);
    preview_title.set_wrap(false);
    auto& camera_preview = preview_frame.add<bgui::image>();
    camera_preview.add_class("camera-preview-image");
    camera_preview.receives_input(false);
    scene_window.add<camera_preview_overlay, bgui::layer::overlay>(
        preview_frame,
        preview_title,
        camera_preview,
        m_camera_preview,
        m_context
    );
    scene_window.add<navigation_hint, bgui::layer::overlay>(m_state);
}

void scene_view::update() {
}

bgui::image& scene_view::image() {
    if (!m_state || !m_state->image)
        throw std::logic_error("scene_view has not been built or is closed");

    return *m_state->image;
}

bool scene_view::is_open() const noexcept {
    return m_state && m_state->image;
}

std::shared_ptr<viewport_input_state> scene_view::input_state() const noexcept {
    return m_input_state;
}

}
