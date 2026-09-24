#pragma once

#include <cstddef>
#include "../../../libs/cpp-bgui/core/include/utils/vec.hpp"
#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

namespace commons {
    template <typename T>
    using vector2 = bgui::vec<2, T>;
    template <typename T>
    using vector3 = bgui::vec<3, T>;
    template <typename T>
    using vector4 = bgui::vec<4, T>;

    using fvec2 = vector2<float>;
    using fvec3 = vector3<float>;
    using fvec4 = vector4<float>;
    using fvector_type2 = fvec2;
    using dvector_type2 = vector2<double>;
    using ivec2 = vector2<int>;
    using fvector_type3 = fvec3;
    using dvector_type3 = vector3<double>;
    using ivector_type3 = vector3<int>;
    using fvector_type4 = fvec4;
    using dvector_type4 = vector4<double>;
    using ivector_type4 = vector4<int>;

    inline glm::vec3 to_glm(const fvec3& value) {
        return {value.x, value.y, value.z};
    }

    inline glm::vec4 to_glm(const fvec4& value) {
        return {value.x, value.y, value.z, value.w};
    }

    inline fvec3 normalized(const fvec3& value) {
        const glm::vec3 result = glm::normalize(to_glm(value));
        return {result.x, result.y, result.z};
    }

    inline btVector3 to_btvec(const fvec3& value) {
        return {value.x, value.y, value.z};
    }
}
