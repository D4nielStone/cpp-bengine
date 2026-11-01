#pragma once

#include "commons_namespace.hpp"
#include "component.hpp"
#include "utils/vec.hpp"

#include <algorithm>
#include <cmath>

namespace COMMONS_NS {
    struct spot_light : public component {
        bgui::vec3 position{0.f, 2.f, -2.f};
        bgui::vec3 direction{1.f, 0.f, 0.f};
        bgui::vec3 color{1.f, 1.f, 1.f};
        float intensity{5.f};
        float constant{1.f};
        float linear{0.09f};
        float quadratic{0.032f};
        float inner_cutoff{12.5f};
        float outer_cutoff{17.5f};

        void set_inner_cutoff(float value) {
            if (std::isfinite(value))
                inner_cutoff = std::clamp(value, 0.f, std::max(0.f, outer_cutoff - 0.1f));
        }
        void set_outer_cutoff(float value) {
            if (std::isfinite(value))
                outer_cutoff = std::clamp(value, inner_cutoff + 0.1f, 89.9f);
        }

        bgui::vec3 get_world_direction() const;
        bgui::vec3 get_world_position() const;
        bool analyze(const rapidjson::Value& value) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override;
        static constexpr mask mask = {COMPONENTE_LUZ_HOLOFOTE};
    };
}
