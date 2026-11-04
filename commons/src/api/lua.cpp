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
#include "components/hud.hpp"
#include "systems/physics_system.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

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
float vector_length(const bgui::vec3& value) {
    return std::hypot(value.x, value.y, value.z);
}

void register_vector_type(sol::state& lua) {
    lua.new_usertype<bgui::vec3>("Vector3",
        sol::call_constructor, sol::factories(
            []() { return bgui::vec3{0.f, 0.f, 0.f}; },
            [](float x, float y, float z) { return bgui::vec3{x, y, z}; }),
        "x", &bgui::vec3::x, "y", &bgui::vec3::y, "z", &bgui::vec3::z,
        sol::meta_function::addition, [](const bgui::vec3& a, const bgui::vec3& b) { return a + b; },
        sol::meta_function::subtraction, [](const bgui::vec3& a, const bgui::vec3& b) { return a - b; },
        sol::meta_function::unary_minus, [](const bgui::vec3& value) { return value * -1.f; },
        sol::meta_function::multiplication, sol::overload(
            [](const bgui::vec3& value, float scalar) { return value * scalar; },
            [](float scalar, const bgui::vec3& value) { return value * scalar; }),
        sol::meta_function::division, [](const bgui::vec3& value, float scalar) {
            if (scalar == 0.f) throw std::domain_error("Cannot divide a Vector3 by zero.");
            return value / scalar;
        },
        "length", vector_length,
        "normalized", [](const bgui::vec3& value) {
            const auto length = vector_length(value);
            return length == 0.f ? bgui::vec3{} : value / length;
        },
        "distance", [](const bgui::vec3& a, const bgui::vec3& b) { return vector_length(a - b); },
        "lerp", [](const bgui::vec3& a, const bgui::vec3& b, float t) {
            return bgui::vec3{std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t)};
        },
        "dot", [](const bgui::vec3& a, const bgui::vec3& b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        },
        "cross", [](const bgui::vec3& a, const bgui::vec3& b) {
            return bgui::vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
        });
}

struct game_object {
    std::weak_ptr<ecs> registry;
    std::uint32_t id;
    std::shared_ptr<transform> get_transform() const {
        auto owner = registry.lock();
        return owner ? owner->get<transform>(id) : nullptr;
    }
    bool is_descendant_of(const game_object& ancestor) const {
        const auto owner = registry.lock();
        if (!owner || owner != ancestor.registry.lock() || !owner->is_alive(ancestor.id))
            return false;
        std::unordered_set<std::uint32_t> visited{id};
        auto current = get_transform();
        while (current && current->parent_id && visited.insert(current->parent_id).second) {
            if (current->parent_id == ancestor.id) return true;
            current = owner->get<transform>(current->parent_id);
        }
        return false;
    }
    bool set_parent(const sol::object& parent, sol::optional<bool> preserve_world) const {
        const auto owner = registry.lock();
        const auto pose = get_transform();
        if (!owner || !pose) return false;
        if (!parent.valid()) return pose->set_parent(0, preserve_world.value_or(true));
        if (!parent.is<game_object>()) return false;
        const auto target = parent.as<game_object>();
        return owner == target.registry.lock() && target.get_transform() &&
            pose->set_parent(target.id, preserve_world.value_or(true));
    }
};

std::shared_ptr<ecs> camera_owner(const camera& value) {
    auto owner = value.reg.lock();
    if (!owner || owner->get<camera>(value.my_object).get() != &value ||
        !owner->has<transform>(value.my_object))
        return nullptr;
    return owner;
}

sol::object trace_camera_ray(camera& value, sol::optional<float> range,
    sol::optional<game_object> ignore, sol::optional<float> x, sol::optional<float> y,
    sol::this_state state) {
    const auto miss = sol::make_object(state, sol::nil);
    const auto owner = camera_owner(value);
    const auto world = sol::state_view(state).registry()["bengine.physics_world"]
        .get<std::weak_ptr<physics_system>>().lock();
    const float max_distance = range.value_or(300.f);
    if (!owner || !world || !std::isfinite(max_distance) || max_distance <= 0.f ||
        x.has_value() != y.has_value() || (ignore && ignore->registry.lock() != owner))
        return miss;

    const auto size = value.viewport_size();
    const bgui::vec3 screen{x.value_or(size.x * 0.5f), y.value_or(size.y * 0.5f), 0.f};
    const auto near_point = value.screen_to_world(screen);
    if (!near_point) return miss;
    const bool orthographic = value.config.flag_orth;
    const auto origin = orthographic ? *near_point : value.position;
    const auto direction = orthographic
        ? bgui::vec3{-value.viewMatrix[0][2], -value.viewMatrix[1][2], -value.viewMatrix[2][2]}
        : *near_point - origin;
    const auto length = vector_length(direction);
    if (!std::isfinite(length) || length <= 0.f) return miss;
    const auto hit = world->cast_scene_ray({origin, direction / length}, max_distance,
        owner, ignore ? ignore->id : 0);
    if (!hit.atingiu) return miss;

    const auto id = static_cast<std::uint32_t>(hit.objetoAtingido->getUserIndex());
    return sol::state_view(state).create_table_with(
        "entity", game_object{owner, id}, "point", hit.pontoDeColisao,
        "normal", hit.normalAtingida, "distance", vector_length(hit.pontoDeColisao - origin));
}

void register_types(sol::state& lua) {
    // Re-registering usertypes invalidates metatables when setup runs again.
    if (lua.registry()["bengine.lua_types_registered"].get_or(false)) return;
    register_vector_type(lua);
    lua.new_usertype<hud_element>("HudElement", sol::no_constructor,
        "text", &hud_element::text, "width", &hud_element::width,
        "height", &hud_element::height, "scale", &hud_element::scale,
        "visible", &hud_element::visible, "position", &hud_element::position,
        "rotation", &hud_element::rotation, "world_scale", &hud_element::world_scale,
        "depth", &hud_element::depth, "ConsumeClick", &hud_element::consume_click);
    lua.new_usertype<hud>("HUD", sol::no_constructor,
        "enabled", &hud::enabled, "Add", &hud::add,
        "count", sol::property([](const hud& h) { return h.elements.size(); }),
        "Get", [](const hud& h, int index) -> std::shared_ptr<hud_element> {
            return index >= 0 && static_cast<size_t>(index) < h.elements.size()
                ? h.elements[index] : nullptr;
        });
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
        "traceRay", trace_camera_ray,
        "worldToScreen", [](camera& value, const bgui::vec3& point) -> std::optional<bgui::vec3> {
            return camera_owner(value) ? value.world_to_screen(point) : std::nullopt;
        },
        "screenToWorld", [](camera& value, const bgui::vec3& point) -> std::optional<bgui::vec3> {
            return camera_owner(value) ? value.screen_to_world(point) : std::nullopt;
        },
        "forward", sol::property([](camera& value) {
            const auto view = value.getViewMatrix();
            return bgui::vec3{-view[0][2], -view[1][2], -view[2][2]};
        }),
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
    lua.new_usertype<game_object>("GameObject", sol::no_constructor,
        "id", sol::readonly(&game_object::id),
        "transform", sol::property(&game_object::get_transform),
        "IsDescendantOf", &game_object::is_descendant_of,
        "SetParent", &game_object::set_parent,
        "AddHUD", [](const game_object& object) -> std::shared_ptr<hud> {
            auto owner = object.registry.lock();
            if (!owner || !owner->is_alive(object.id)) return nullptr;
            if (!owner->has<hud>(object.id)) {
                entity ent; ent.id = object.id;
                owner->add<hud>(ent);
            }
            return owner->get<hud>(object.id);
        },
        "GetComponent", [](const game_object& object, const std::string& name, sol::this_state state) -> sol::object {
            auto owner = object.registry.lock();
            if (!owner) return sol::make_object(state, sol::nil);
            if (name == "Transform") return sol::make_object(state, object.get_transform());
            if (name == "HUD") return sol::make_object(state, owner->get<hud>(object.id));
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
    lua.registry()["bengine.lua_types_registered"] = true;
}
}

void setUpApi(sol::state& lua, const std::shared_ptr<ecs>& registry, std::uint32_t id) {
    setUpApi(lua, registry, id, {});
}

void setUpApi(sol::state& lua, const std::shared_ptr<ecs>& registry, std::uint32_t id,
    const std::weak_ptr<physics_system>& physics_world) {
    register_types(lua);
    lua.registry()["bengine.physics_world"] = physics_world;
    lua.set_function("Destroy", [](const std::shared_ptr<physics>& body) {
        if (body) {
            if (const auto registry = body->reg.lock(); registry &&
                registry->get<physics>(body->my_object) == body)
                registry->remove<physics>(body->my_object);
        }
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
