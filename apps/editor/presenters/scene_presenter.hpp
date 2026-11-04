#pragma once

#include "editor_api.hpp"
#include "editor_viewport.hpp"

namespace commons { class camera; class window; }
namespace bgui { class image; class window; }

namespace editor::ui {

struct viewport_input_state;
struct camera_preview_state;

class scene_presenter {
public:
    struct preview_bounds {
        int x, y, width, height, gizmo_height;
    };

    struct preview {
        viewport_frame frame;
        std::string title;
    };

    scene_presenter(commons::window& window, std::shared_ptr<commons::camera> camera,
        std::shared_ptr<editor_context> context);

    viewport_frame initialize_viewport();
    void attach_overlays(bgui::window& window, bgui::image& image);
    std::optional<editor_api::entity_id> pick(bgui::image& image, int x, int y) const;
    bool needs_camera_render(std::uint32_t entity) const noexcept;
    std::optional<preview> preview_frame(int width, int height) const;
    std::shared_ptr<viewport_input_state> input_state() const noexcept { return m_input_state; }
    std::optional<preview_bounds> preview_area(float scale) const;
    bool orbital() const noexcept;
    void toggle_orbital();

private:
    commons::window& m_window;
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<viewport_input_state> m_input_state;
    std::shared_ptr<camera_preview_state> m_camera_preview;
};

}
