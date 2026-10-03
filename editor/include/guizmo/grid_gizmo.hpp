#pragma once

#include <utils/vec.hpp>

#include "commons_namespace.hpp"

namespace COMMONS_NS {
    class camera;
}

namespace editor {
    class grid_gizmo {
    public:
        bool enabled{true};
        float spacing{1.f};
        float extent{50.f};

        void draw(const COMMONS_NS::camera& camera, const bgui::vec4i& viewport) const;
    };
}