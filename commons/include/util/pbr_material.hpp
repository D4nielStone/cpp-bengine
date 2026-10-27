#pragma once

#include "util/material.hpp"
#include <array>
#include <string_view>

namespace commons {
struct pbr_factor {
    const char* label;
    const char* name;
    float fallback;
};
inline constexpr std::array<pbr_factor, 3> pbr_factors{{
    {"Metallic", "metallic", 0.f},
    {"Roughness", "roughness", 0.5f},
    {"Ambient Occlusion", "ao", 1.f}
}};
struct pbr_map {
    const char* label;
    const char* name;
};
inline constexpr std::array<pbr_map, 5> pbr_maps{{
    {"Albedo Map", "tex_albedo"},
    {"Normal Map", "tex_normal"},
    {"Metallic Map", "tex_metallic"},
    {"Roughness Map", "tex_roughness"},
    {"AO Map", "tex_ao"}
}};

inline float pbr_value(const material& material, const pbr_factor& factor) {
    const auto found = material.uniforms.find(std::string("material.") + factor.name);
    if (found == material.uniforms.end())
        return factor.fallback;
    const auto& value = found->second;
    if (value.m_type == uniform::type::Float)
        return value.valor.f;
    if (value.m_type == uniform::type::Float_ptr && value.ponteiro.f_ptr)
        return *value.ponteiro.f_ptr;
    return factor.fallback;
}
}
