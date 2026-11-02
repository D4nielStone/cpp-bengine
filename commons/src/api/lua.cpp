#include "api/lua.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/physics.hpp"
#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/sound_listener.hpp"
#include "components/sound_player.hpp"
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

void setUpApi(sol::state& lua, const std::shared_ptr<ecs>& registry, std::uint32_t id) {
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
        "enabled", &renderer::visible, "visible", &renderer::visible,
        "castShadows", &renderer::cast_shadows, "receiveShadows", &renderer::receive_shadows);
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
        "fieldOfView", sol::property([](camera& value) { return value.config.fov; },
            [](camera& value, float field_of_view) { value.config.fov = field_of_view; }),
        "aspect", sol::property([](camera& value) { return value.config.aspecto; }),
        "nearClipPlane", sol::property([](camera& value) { return value.config.corte_curto; },
            [](camera& value, float near_clip) { value.config.corte_curto = near_clip; }),
        "farClipPlane", sol::property([](camera& value) { return value.config.corte_longo; },
            [](camera& value, float far_clip) { value.config.corte_longo = far_clip; }),
        "orthographic", sol::property([](camera& value) { return value.config.flag_orth; },
            [](camera& value, bool enabled) { value.config.flag_orth = enabled; }),
        "orthographicSize", sol::property([](camera& value) { return value.config.scale; },
            [](camera& value, float scale) { value.config.scale = scale; }),
        "backgroundColor", sol::property([](camera& value) -> color& { return value.config.sky_solid_color; },
            [](camera& value, const color& background) { value.config.sky_solid_color = background; }),
        "useSkybox", sol::property([](camera& value) { return value.config.use_skybox; },
            [](camera& value, bool enabled) { value.config.use_skybox = enabled; }),
        "skyboxImage", sol::property([](camera& value) -> std::string& { return value.config.skybox_image; },
            [](camera& value, const std::string& image) { value.config.skybox_image = image; }),
        "fogEnabled", sol::property([](camera& value) { return value.config.fog_enabled; },
            [](camera& value, bool enabled) { value.config.fog_enabled = enabled; }),
        "fogColor", sol::property([](camera& value) -> color& { return value.config.fog_color; },
            [](camera& value, const color& fog) { value.config.fog_color = fog; }),
        "fogDensity", sol::property([](camera& value) { return value.config.fog_density; },
            [](camera& value, float density) { value.config.fog_density = density; }));
    lua.new_usertype<directional_light>("DirectionalLight", sol::no_constructor,
        "direction", &directional_light::direction, "ambient", &directional_light::ambient,
        "color", &directional_light::color, "intensity", &directional_light::intensity,
        "shadows", &directional_light::shadows, "shadowDistance", &directional_light::shadow_distance,
        "shadowBias", &directional_light::shadow_bias, "shadowStrength", &directional_light::shadow_strength);
    lua.new_usertype<point_light>("PointLight", sol::no_constructor,
        "position", &point_light::position, "color", &point_light::color,
        "intensity", &point_light::intensity, "constant", &point_light::constant,
        "linear", &point_light::linear, "quadratic", &point_light::quadratic);
    lua.new_usertype<spot_light>("SpotLight", sol::no_constructor,
        "position", &spot_light::position, "direction", &spot_light::direction,
        "color", &spot_light::color, "intensity", &spot_light::intensity,
        "constant", &spot_light::constant, "linear", &spot_light::linear,
        "quadratic", &spot_light::quadratic,
        "worldPosition", sol::property(&spot_light::get_world_position),
        "worldDirection", sol::property(&spot_light::get_world_direction),
        "innerCutoff", sol::property(
            [](const spot_light& light) { return light.inner_cutoff; },
            &spot_light::set_inner_cutoff),
        "outerCutoff", sol::property(
            [](const spot_light& light) { return light.outer_cutoff; },
            &spot_light::set_outer_cutoff),
        "GetWorldDirection", &spot_light::get_world_direction,
        "SetInnerCutoff", &spot_light::set_inner_cutoff,
        "SetOuterCutoff", &spot_light::set_outer_cutoff,
        "shadows", &spot_light::shadows, "shadowDistance", &spot_light::shadow_distance,
        "shadowBias", &spot_light::shadow_bias, "shadowStrength", &spot_light::shadow_strength);
    lua.new_usertype<sound_listener>("SoundListener", sol::no_constructor,
        "enabled", &sound_listener::enabled);
    lua.new_usertype<sound_player>("SoundPlayer", sol::no_constructor,
        "clip", &sound_player::clip,
        "volume", &sound_player::volume,
        "pitch", &sound_player::pitch,
        "radius", &sound_player::radius,
        "looping", &sound_player::looping,
        "playOnAwake", &sound_player::play_on_awake);
    lua.set_function("Destroy", [](const std::shared_ptr<physics>& body) {
        if (body) {
            if (const auto registry = body->reg.lock())
                registry->remove<physics>(body->my_object);
        }
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
            if (name == "SpotLight") return sol::make_object(state, owner->get<spot_light>(object.id));
            if (name == "SoundListener") return sol::make_object(state, owner->get<sound_listener>(object.id));
            if (name == "SoundPlayer") return sol::make_object(state, owner->get<sound_player>(object.id));
            if (name == "Rigidbody") return sol::make_object(state, owner->get<physics>(object.id));
            return sol::make_object(state, sol::nil);
        });
    const std::weak_ptr<ecs> weak_registry = registry;
    lua["GameObject"]["Find"] = sol::overload(
        [weak_registry](std::uint32_t entity_id) -> sol::optional<game_object> {
            auto owner = weak_registry.lock();
            if (!owner || !owner->is_alive(entity_id)) return sol::nullopt;
            return game_object{owner, entity_id};
        },
        [weak_registry](const std::string& name) -> sol::optional<game_object> {
            auto owner = weak_registry.lock();
            if (!owner) return sol::nullopt;
            const auto found = owner->find_by_name(name);
            if (!found) return sol::nullopt;
            return game_object{owner, *found};
        });
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

namespace commons::api {
void setUnityApi(sol::state& lua, const std::shared_ptr<ecs>& registry, std::uint32_t id) {
    setUpApi(lua, registry, id);
}
}
