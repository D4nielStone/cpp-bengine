#pragma once

#include <utils/vec.hpp>

#include "commons_namespace.hpp"

namespace COMMONS_NS {
    class camera;
}

namespace editor {
    class grid_gizmo {
    public:
        grid_gizmo() = default;
        ~grid_gizmo();

        grid_gizmo(const grid_gizmo&) = delete;
        grid_gizmo& operator=(const grid_gizmo&) = delete;

        bool enabled{true};
        float spacing{1.f};
        float extent{50.f};

        void draw(const COMMONS_NS::camera& camera, const bgui::vec4i& viewport);

    private:
        unsigned int m_program{0};
        unsigned int m_vao{0};
        unsigned int m_vbo{0};
    };
}