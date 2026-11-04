#pragma once

#include <array>

namespace editor::ui {

struct primitive_description { const char* name; const char* asset; };
inline constexpr std::array primitive_entities{
    primitive_description{"Cubo", "primitive-cube.bmodel"}, primitive_description{"Esfera", "sphere.bmodel"},
    primitive_description{"Plano", "plane.bmodel"}, primitive_description{"Cilindro", "cylinder.bmodel"},
    primitive_description{"Cápsula", "capsule.bmodel"}
};

}
