#include "scene_view.hpp"

#include "../presenters/scene_presenter.hpp"

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
    bgui::button* mode_button{nullptr};
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
        : bgui::text("Drag: look / orbit    Shift + Left Ctrl + drag: pan\n"
                     "Scroll: dolly    Left Ctrl + WASD / QE: move", 0.25f),
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

class tools_container final : public bgui::linear {
public:
    explicit tools_container(std::shared_ptr<scene_view_state> state)
        : bgui::linear(bgui::orientation::horizontal), m_state(std::move(state)) {
        add_class("scene-tools-container");
        set_flex(false);
        style.layout.padding = bgui::vec4i{4};
    }
    void on_update() override {
        if (!m_state->image) return;
        const auto rect = m_state->image->processed_rect();
        const float scale = bgui::get_global_scale();
        set_final_rect(rect.x + int(8 * scale), rect.y + int(8 * scale),
            std::min(std::max(0, rect.z - int(16 * scale)), int(160 * scale)), int(34 * scale));
    }
private:
    std::shared_ptr<scene_view_state> m_state;
};

class camera_preview_overlay final : public bgui::element {
public:
    camera_preview_overlay(bgui::linear& frame, bgui::text& title, bgui::image& image,
        std::shared_ptr<scene_presenter> presenter)
        : m_frame(&frame), m_title(&title), m_image(&image),
          m_presenter(std::move(presenter)) {
        receives_input(false);
        set_flex(false);
        style.visual.visible = false;
    }

    void on_update() override {
        const float ui_scale = bgui::get_global_scale();
        const auto viewport = m_presenter->preview_area(ui_scale);
        if (!viewport) {
            m_frame->set_enable(false);
            return;
        }

        const int margin = static_cast<int>(12.f * ui_scale);
        const int padding = static_cast<int>(6.f * ui_scale);
        const int header = static_cast<int>(18.f * ui_scale);
        const float available_width = viewport->width - 2 * (margin + padding);
        const int top_offset = viewport->gizmo_height + margin;
        const float available_height = viewport->height - top_offset - 2 * (margin + padding) - header;
        const float scale = std::min({1.f, available_width / (240.f * ui_scale),
            available_height / (150.f * ui_scale)});
        if (scale <= 0.f) {
            m_frame->set_enable(false);
            return;
        }
        const int preview_width = std::max(1, static_cast<int>(240 * ui_scale * scale));
        const int preview_height = std::max(1, static_cast<int>(150 * ui_scale * scale));
        const auto preview = m_presenter->preview_frame(preview_width, preview_height);
        if (!preview) {
            m_frame->set_enable(false);
            return;
        }

        const int frame_width = preview_width + 2 * padding;
        const int frame_height = preview_height + 2 * padding + header;
        const int preview_x = std::max(
            viewport->x + margin,
            viewport->x + viewport->width - frame_width - margin
        );
        m_title->set_buffer(preview->title);
        m_image->set_external_texture(preview->frame.texture,
            {static_cast<float>(preview->frame.width), static_cast<float>(preview->frame.height)});
        m_frame->set_final_rect(preview_x, viewport->y + margin + top_offset, frame_width, frame_height);
        m_image->set_size(preview_width / ui_scale, preview_height / ui_scale);
        m_frame->set_enable(true);
    }

private:
    bgui::linear* m_frame;
    bgui::text* m_title;
    bgui::image* m_image;
    std::shared_ptr<scene_presenter> m_presenter;
};

class scene_view_surface final : public bgui::linear {
public:
    explicit scene_view_surface(std::shared_ptr<scene_view_state> state)
        : bgui::linear(bgui::orientation::vertical),
          m_state(std::move(state)) {
    }

    ~scene_view_surface() override {
        m_state->image = nullptr;
        m_state->mode_button = nullptr;
    }

private:
    std::shared_ptr<scene_view_state> m_state;
};


}

scene_view::scene_view(
    commons::window& window,
    std::shared_ptr<commons::camera> camera,
    std::shared_ptr<editor_context> context
)
    : m_presenter(std::make_shared<scene_presenter>(window, std::move(camera), std::move(context))),
      m_state(std::make_shared<scene_view_state>()) {}


std::optional<editor_context::entity_id> scene_view::entity_at(const bgui::vec2i& position) const {
    if (!is_open()) return std::nullopt;
    return m_presenter->pick(*m_state->image, position.x, position.y);
}

void scene_view::build(bgui::dock& dock) {
    const auto frame = m_presenter->initialize_viewport();

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

    image.set_external_texture(frame.texture, {static_cast<float>(frame.width),
        static_cast<float>(frame.height)});
    m_presenter->attach_overlays(scene_window, image);

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
        m_presenter
    );
    auto& tools = scene_window.add<tools_container, bgui::layer::overlay>(m_state);
    auto& mode = tools.add<bgui::button>("View: Normal", 0.28f, [] {});
    mode.style.layout.require_size(150.f, 26.f);
    mode.add<bgui::icon>("eye", 0.28f).receives_input(false);
    m_state->mode_button = &mode;
    mode.set_function([presenter = m_presenter] {
        presenter->toggle_orbital();
    });
    scene_window.add<navigation_hint, bgui::layer::overlay>(m_state);
}

void scene_view::update() {
    if (m_state->mode_button)
        m_state->mode_button->get_label().set_buffer(
            m_presenter->orbital() ? "View: Orbital (O)" : "View: Normal (O)");
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
    return m_presenter->input_state();
}

bool scene_view::needs_camera_render(std::uint32_t entity) const noexcept {
    return is_open() && m_presenter->needs_camera_render(entity);
}

}
