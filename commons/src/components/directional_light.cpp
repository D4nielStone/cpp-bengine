#include "components/directional_light.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include <unordered_set>

using namespace COMMONS_NS;

bool directional_light::analyze(const rapidjson::Value& value)
{
    if(!value.HasMember("direction") || !value["direction"].IsArray()) return false;
    if(!value.HasMember("ambient") || !value["ambient"].IsArray()) return false;
    if(!value.HasMember("color") || !value["color"].IsArray()) return false;
    if(!value.HasMember("intensity") || !value["intensity"].IsFloat()) return false;

	direction = {value["direction"].GetArray()[0].GetFloat(), value["direction"].GetArray()[1].GetFloat(), value["direction"].GetArray()[2].GetFloat()};
    ambient = {value["ambient"].GetArray()[0].GetFloat(), value["ambient"].GetArray()[1].GetFloat(), value["ambient"].GetArray()[2].GetFloat()};
    color = {value["color"].GetArray()[0].GetFloat(), value["color"].GetArray()[1].GetFloat(), value["color"].GetArray()[2].GetFloat()};
    intensity = value["intensity"].GetFloat();

    return true;
};
bool directional_light::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const
{
    rapidjson::Value direction_v(rapidjson::kArrayType);
    rapidjson::Value cor_v(rapidjson::kArrayType);
    rapidjson::Value ambient_v(rapidjson::kArrayType);

    direction_v.PushBack(direction.x , allocator);
    direction_v.PushBack(direction.y , allocator);
    direction_v.PushBack(direction.z , allocator);

    cor_v.PushBack(color.x , allocator);
    cor_v.PushBack(color.y , allocator);
    cor_v.PushBack(color.z , allocator);

    ambient_v.PushBack(ambient.x , allocator);
    ambient_v.PushBack(ambient.y , allocator);
    ambient_v.PushBack(ambient.z , allocator);

    value.AddMember("direction", direction_v, allocator);
    value.AddMember("ambient", ambient_v, allocator);
    value.AddMember("color", cor_v, allocator);
    value.AddMember("intensity", intensity, allocator);
    return true;
}

bgui::vec3 directional_light::get_world_direction() const {
    if (!reg) return direction;
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
    std::unordered_set<std::uint32_t> visited;
    auto id = my_object;
    while (visited.insert(id).second) {
        const auto tr = reg->get<transform>(id);
        if (!tr) break;
        const auto r = glm::radians(glm::vec3{tr->rotation.x, tr->rotation.y, tr->rotation.z});
        const auto local = glm::angleAxis(r.x, glm::vec3{1, 0, 0}) *
            glm::angleAxis(r.y, glm::vec3{0, 1, 0}) *
            glm::angleAxis(r.z, glm::vec3{0, 0, 1});
        orientation = local * orientation;
        if (!tr->parent_id) break;
        id = tr->parent_id;
    }
    const auto result = orientation * glm::vec3{direction.x, direction.y, direction.z};
    return {result.x, result.y, result.z};
}
