#pragma once

#include "editor_context.hpp"
#include "panel.hpp"

#include <cstdint>
#include <memory>

namespace editor::ui {

struct game_view_state;

class game_view final : public panel {
public:
    explicit game_view(std::shared_ptr<editor_context> context);

    void build(bgui::dock& dock) override;
    void update() override;

    bool needs_camera_render(std::uint32_t entity) const noexcept;

private:
    std::shared_ptr<game_view_state> m_state;
};

}
