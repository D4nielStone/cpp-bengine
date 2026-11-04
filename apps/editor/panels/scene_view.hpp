#pragma once

#include "editor_context.hpp"
#include "lay/dock.hpp"
#include "panel.hpp"
#include "viewport_input_state.hpp"

#include <elem/image.hpp>

namespace commons { class camera; class window; }

namespace editor::ui {

class scene_presenter;

struct scene_view_state;

class scene_view final : public panel {
public:
    scene_view(
        commons::window& window,
        std::shared_ptr<commons::camera> camera,
        std::shared_ptr<editor_context> context
    );

    void build(bgui::dock& dock) override;
    void update() override;

    bgui::image& image();
    bool is_open() const noexcept;
    bool needs_camera_render(std::uint32_t entity) const noexcept;
    std::optional<editor_context::entity_id> entity_at(const bgui::vec2i& position) const;
    std::shared_ptr<viewport_input_state> input_state() const noexcept;

private:
    std::shared_ptr<scene_presenter> m_presenter;
    std::shared_ptr<scene_view_state> m_state;
};

}
