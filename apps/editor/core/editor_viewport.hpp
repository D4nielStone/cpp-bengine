#pragma once

#include <cstdint>

namespace editor::ui {

struct viewport_frame {
    std::uint32_t texture{};
    int width{};
    int height{};
};

class editor_viewport {
public:
    virtual ~editor_viewport() = default;
    virtual viewport_frame frame() const = 0;
    virtual void request_size(int width, int height) = 0;
};

}
