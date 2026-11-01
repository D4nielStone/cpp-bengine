#include "components/spot_light.hpp"

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"

#include <cmath>
#include <unordered_set>

using namespace COMMONS_NS;

namespace {
bool read_vector3(const rapidjson::Value& object, const char* name, bgui::vec3& destination) {
    if (!object.HasMember(name) || !object[name].IsArray() || object[name].Size() != 3)
        return false;
    for (const auto& value : object[name].GetArray())
        if (!value.IsNumber() || !std::isfinite(value.GetFloat()))
            return false;
    destination = {
        object[name][0].GetFloat(),
        object[name][1].GetFloat(),
        object[name][2].GetFloat()
    };
    return true;
}

bool read_optional_number(
    const rapidjson::Value& object,
    const char* name,
    float& destination
) {
    if (!object.HasMember(name))
        return true;
    if (!object[name].IsNumber() || !std::isfinite(object[name].GetFloat()))
        return false;
    destination = object[name].GetFloat();
    return true;
}
}

bool spot_light::analyze(const rapidjson::Value& value) {
    if (!value.IsObject() ||
        !read_vector3(value, "position", position) ||
        !read_vector3(value, "direction", direction) ||
        !read_vector3(value, "color", color) ||
        !read_optional_number(value, "intensity", intensity) ||
        !read_optional_number(value, "constant", constant) ||
        !read_optional_number(value, "linear", linear) ||
        !read_optional_number(value, "quadratic", quadratic) ||
        !read_optional_number(value, "inner_cutoff", inner_cutoff) ||
        !read_optional_number(value, "outer_cutoff", outer_cutoff))
        return false;

    shadows = true;
    shadow_distance = 50.f;
    shadow_bias = 0.001f;
    shadow_strength = 1.f;
    if (value.HasMember("shadows")) {
        if (!value["shadows"].IsBool()) return false;
        shadows = value["shadows"].GetBool();
    }
    if (!read_optional_number(value, "shadow_distance", shadow_distance) ||
        !read_optional_number(value, "shadow_bias", shadow_bias) ||
        !read_optional_number(value, "shadow_strength", shadow_strength) ||
        shadow_distance < 1.f || shadow_distance > 10000.f ||
        shadow_bias < 0.f || shadow_bias > 0.1f ||
        shadow_strength < 0.f || shadow_strength > 1.f)
        return false;

    return inner_cutoff >= 0.f && inner_cutoff < outer_cutoff && outer_cutoff < 90.f;
}

bool spot_light::serialize(
    rapidjson::Value& value,
    rapidjson::Document::AllocatorType& allocator
) const {
    const auto finite_vector = [](const bgui::vec3& vector) {
        return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
    };
    if (!value.IsObject() || !finite_vector(position) || !finite_vector(direction) ||
        !finite_vector(color) || !std::isfinite(intensity) || !std::isfinite(constant) ||
        !std::isfinite(linear) || !std::isfinite(quadratic) ||
        !std::isfinite(inner_cutoff) || !std::isfinite(outer_cutoff) ||
        !std::isfinite(shadow_distance) || !std::isfinite(shadow_bias) || !std::isfinite(shadow_strength) ||
        shadow_distance < 1.f || shadow_distance > 10000.f ||
        shadow_bias < 0.f || shadow_bias > 0.1f || shadow_strength < 0.f || shadow_strength > 1.f ||
        inner_cutoff < 0.f || inner_cutoff >= outer_cutoff || outer_cutoff >= 90.f)
        return false;

    const auto write_vector = [&](const char* name, const bgui::vec3& vector) {
        rapidjson::Value array(rapidjson::kArrayType);
        array.PushBack(vector.x, allocator);
        array.PushBack(vector.y, allocator);
        array.PushBack(vector.z, allocator);
        value.AddMember(rapidjson::Value(name, allocator), array, allocator);
    };

    write_vector("position", position);
    write_vector("direction", direction);
    write_vector("color", color);
    value.AddMember("intensity", intensity, allocator);
    value.AddMember("constant", constant, allocator);
    value.AddMember("linear", linear, allocator);
    value.AddMember("quadratic", quadratic, allocator);
    value.AddMember("inner_cutoff", inner_cutoff, allocator);
    value.AddMember("outer_cutoff", outer_cutoff, allocator);
    value.AddMember("shadows", shadows, allocator);
    value.AddMember("shadow_distance", shadow_distance, allocator);
    value.AddMember("shadow_bias", shadow_bias, allocator);
    value.AddMember("shadow_strength", shadow_strength, allocator);
    return true;
}

bgui::vec3 spot_light::get_world_direction() const {
    const auto registry = reg.lock();
    if (!registry)
        return direction;

    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
    std::unordered_set<std::uint32_t> visited;
    auto id = my_object;
    while (visited.insert(id).second) {
        const auto transform = registry->get<commons::transform>(id);
        if (!transform)
            break;
        const auto rotation = glm::radians(glm::vec3{
            transform->rotation.x, transform->rotation.y, transform->rotation.z
        });
        const auto local = registry->get<commons::camera>(id)
            ? glm::angleAxis(-rotation.y, glm::vec3{0, 1, 0}) *
                glm::angleAxis(rotation.x, glm::vec3{0, 0, 1})
            : glm::angleAxis(rotation.x, glm::vec3{1, 0, 0}) *
                glm::angleAxis(rotation.y, glm::vec3{0, 1, 0}) *
                glm::angleAxis(rotation.z, glm::vec3{0, 0, 1});
        orientation = local * orientation;
        if (!transform->parent_id)
            break;
        id = transform->parent_id;
    }

    const auto world = orientation * glm::vec3{direction.x, direction.y, direction.z};
    return {world.x, world.y, world.z};
}

bgui::vec3 spot_light::get_world_position() const {
    const auto registry = reg.lock();
    if (!registry)
        return position;
    const auto transform = registry->get<commons::transform>(my_object);
    if (!transform)
        return position;

    const auto world = transform->get_world_model_matrix() *
        glm::vec4{position.x, position.y, position.z, 1.f};
    return {world.x, world.y, world.z};
}
