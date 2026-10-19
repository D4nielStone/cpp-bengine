#pragma once

#include "components/camera.hpp"
#include "editor_context.hpp"
#include "lay/dock.hpp"
#include "os/window.hpp"
#include "panel.hpp"
#include "viewport_input_state.hpp"

#include <elem/image.hpp>

namespace editor::ui {

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
    std::shared_ptr<viewport_input_state> input_state() const noexcept;

private:
    commons::window& m_window;
    std::shared_ptr<commons::camera> m_camera;
    std::shared_ptr<editor_context> m_context;
    std::shared_ptr<viewport_input_state> m_input_state;
    bgui::image* m_image = nullptr;
};

}