#include "api/lua.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/physics.hpp"
#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "core/ecs.hpp"

namespace commons::api {

void setClasses(sol::state& lua) {
    lua.open_libraries(sol::lib::math);
}

void setNamespaces(sol::state& lua) {
    lua["bubble"].get_or_create<sol::table>();
}

}

namespace commons::api {
namespace {
struct game_object {
    std::weak_ptr<ecs> registry;
    std::uint32_t id;
    std::shared_ptr<transform> get_transform() const {
        auto owner = registry.lock();
        return owner ? owner->get<transform>(id) : nullptr;
    }
};
}

void setUnityApi(sol::state& lua, const std::shared_ptr<ecs>& registry, std::uint32_t id) {
    lua.new_usertype<bgui::vec3>("Vector3",
        sol::call_constructor, sol::factories(
            []() { return bgui::vec3{0.f, 0.f, 0.f}; },
            [](float x, float y, float z) { return bgui::vec3{x, y, z}; }),
        "x", &bgui::vec3::x, "y", &bgui::vec3::y, "z", &bgui::vec3::z);
    lua.new_usertype<transform>("Transform", sol::no_constructor,
        "position", sol::property(&transform::get_world_position, &transform::set_world_position),
        "localPosition", sol::property(&transform::get_position, &transform::set_position),
        "eulerAngles", sol::property(&transform::get_world_rotation, &transform::set_world_rotation),
        "localEulerAngles", sol::property(&transform::get_rotation, &transform::set_rotation),
        "localScale", sol::property(&transform::get_scale, &transform::set_scale),
        "Translate", [](transform& value, const bgui::vec3& offset) {
            const auto euler = value.get_rotation();
            const auto orientation =
                glm::angleAxis(glm::radians(euler.x), glm::vec3{1.f, 0.f, 0.f}) *
                glm::angleAxis(glm::radians(euler.y), glm::vec3{0.f, 1.f, 0.f}) *
                glm::angleAxis(glm::radians(euler.z), glm::vec3{0.f, 0.f, 1.f});
            const auto rotated = orientation * glm::vec3{offset.x, offset.y, offset.z};
            value.move({rotated.x, rotated.y, rotated.z});
        },
        "Rotate", &transform::rotate);
    lua.new_usertype<renderer>("Renderer", sol::no_constructor,
        "enabled", &renderer::visible, "visible", &renderer::visible);
    lua.new_usertype<physics>("Rigidbody", sol::no_constructor,
        "mass", sol::property([](const physics& p) { return p.m_massa; }, &physics::setMass),
        "friction", sol::property([](const physics& p) { return p.friction; }, &physics::setFriccao),
        "restitution", sol::property([](const physics& p) { return p.restitution; }, &physics::setRestituicao),
        "velocity", sol::property(&physics::getVelocidade, &physics::setVelocidade),
        "AddForce", &physics::setForca,
        "SetLinearFactor", &physics::setFatorLinear,
        "SetAngularFactor", &physics::setFatorAngular);
    lua.new_usertype<color>("Color", sol::call_constructor,
        sol::constructors<color(), color(const float&, const float&, const float&, const float&)>(),
        "r", &color::r, "g", &color::g, "b", &color::b, "a", &color::a);
    lua.new_usertype<camera>("Camera", sol::no_constructor,
        "fieldOfView", &camera::fov, "aspect", &camera::aspecto,
        "nearClipPlane", &camera::corte_curto, "farClipPlane", &camera::corte_longo,
        "orthographic", &camera::flag_orth, "orthographicSize", &camera::scale,
        "backgroundColor", &camera::ceu, "useSkybox", &camera::m_use_skybox);
    lua.new_usertype<directional_light>("DirectionalLight", sol::no_constructor,
        "direction", &directional_light::direction, "ambient", &directional_light::ambient,
        "color", &directional_light::color, "intensity", &directional_light::intensity);
    lua.new_usertype<point_light>("PointLight", sol::no_constructor,
        "position", &point_light::position, "color", &point_light::color,
        "intensity", &point_light::intensity, "constant", &point_light::constant,
        "linear", &point_light::linear, "quadratic", &point_light::quadratic);
    lua.set_function("Destroy", [](const std::shared_ptr<physics>& body) {
        if (body && body->reg) body->reg->remove<physics>(body->my_object);
    });
    lua.new_usertype<game_object>("GameObject", sol::no_constructor,
        "id", sol::readonly(&game_object::id),
        "transform", sol::property(&game_object::get_transform),
        "GetComponent", [](const game_object& object, const std::string& name, sol::this_state state) -> sol::object {
            auto owner = object.registry.lock();
            if (!owner) return sol::make_object(state, sol::nil);
            if (name == "Transform") return sol::make_object(state, object.get_transform());
            if (name == "Renderer") return sol::make_object(state, owner->get<renderer>(object.id));
            if (name == "Camera") return sol::make_object(state, owner->get<camera>(object.id));
            if (name == "DirectionalLight") return sol::make_object(state, owner->get<directional_light>(object.id));
            if (name == "PointLight") return sol::make_object(state, owner->get<point_light>(object.id));
            if (name == "Rigidbody") return sol::make_object(state, owner->get<physics>(object.id));
            return sol::make_object(state, sol::nil);
        });
    const std::weak_ptr<ecs> weak_registry = registry;
    lua["GameObject"]["Find"] = [weak_registry](std::uint32_t entity_id)
        -> sol::optional<game_object> {
        auto owner = weak_registry.lock();
        if (!owner || !owner->is_alive(entity_id)) return sol::nullopt;
        return game_object{owner, entity_id};
    };
    lua["gameObject"] = game_object{registry, id};
    lua["transform"] = registry ? registry->get<transform>(id) : nullptr;
    auto bubble = lua["bubble"].get<sol::table>();
    auto input = lua.create_named_table("Input");
    input["GetKey"] = bubble["input"]["is_key_down"];
    input["GetMouseDelta"] = bubble["input"]["mouse_delta"];
    input["SetMouseCaptured"] = bubble["input"]["set_mouse_captured"];
    input.set_function("GetAxis", [input](const std::string& axis) -> float {
        sol::function held = input["GetKey"];
        if (axis == "Horizontal") return float(held("D").get<bool>()) - float(held("A").get<bool>());
        if (axis == "Vertical") return float(held("W").get<bool>()) - float(held("S").get<bool>());
        return 0.f;
    });
    auto debug = lua.create_named_table("Debug");
    debug["Log"] = bubble["log"];
    lua.create_named_table("Time", "deltaTime", 0.f, "time", 0.f);
}
}
