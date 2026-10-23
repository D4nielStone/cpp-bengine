#include "components/code.hpp"

#include "api/lua.hpp"
#include "api/mat.hpp"
#include "debugging/debug.hpp"
#include "os/system.hpp"
#include "os/window.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "components/renderer.hpp"

#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <tuple>

using namespace COMMONS_NS;

namespace {

struct cursor_state {
    double x{0.0};
    double y{0.0};
    bool initialized{false};
};

}

code::code(const std::string& file) : file(file) {
    arquivoCompleto = file;

    estado_lua.open_libraries(sol::lib::base);

    api::setClasses(estado_lua);
    api::setNamespaces(estado_lua);
}

void code::setup() {
    auto bubble = estado_lua["bubble"].get_or_create<sol::table>();
    bubble["meuID"] = my_object;
    bubble.set_function(
        "log",
        [](const std::string& message) {
            std::clog << "[lua] " << message << '\n';
        }
    );

    const std::weak_ptr<ecs> weak_registry = reg;
    auto entity_api = estado_lua.create_table();
    entity_api.set_function(
        "get_position",
        [weak_registry](const std::uint32_t id) {
            const auto registry = weak_registry.lock();
            if (!registry)
                return std::tuple<float, float, float>{0.f, 0.f, 0.f};
            const auto entity_transform = registry->get<transform>(id);
            if (!entity_transform)
                return std::tuple<float, float, float>{0.f, 0.f, 0.f};
            const auto position = entity_transform->get_position();
            return std::tuple<float, float, float>{
                position.x,
                position.y,
                position.z
            };
        }
    );
    entity_api.set_function(
        "set_visible",
        [weak_registry](const std::uint32_t id, const bool visible) {
            const auto registry = weak_registry.lock();
            if (!registry)
                return;
            if (const auto renderer = registry->get<commons::renderer>(id))
                renderer->visible = visible;
        }
    );
    entity_api.set_function(
        "remove_physics",
        [weak_registry](const std::uint32_t id) {
            if (const auto registry = weak_registry.lock())
                registry->remove<commons::physics>(id);
        }
    );
    bubble["entity"] = entity_api;

    const auto mouse_position = std::make_shared<cursor_state>();
    auto input_api = estado_lua.create_table();
    input_api.set_function(
        "is_key_down",
        [](const std::string& key) {
            auto* window = glfwGetCurrentContext();
            if (!window)
                return false;

            int glfw_key = GLFW_KEY_UNKNOWN;
            if (key == "W" || key == "w")
                glfw_key = GLFW_KEY_W;
            else if (key == "A" || key == "a")
                glfw_key = GLFW_KEY_A;
            else if (key == "S" || key == "s")
                glfw_key = GLFW_KEY_S;
            else if (key == "D" || key == "d")
                glfw_key = GLFW_KEY_D;
            else if (key == "SPACE" || key == "space")
                glfw_key = GLFW_KEY_SPACE;

            return glfw_key != GLFW_KEY_UNKNOWN &&
                glfwGetKey(window, glfw_key) == GLFW_PRESS;
        }
    );
    input_api.set_function(
        "set_mouse_captured",
        [mouse_position](const bool captured) {
            auto* window = glfwGetCurrentContext();
            if (!window)
                return;

            glfwSetInputMode(
                window,
                GLFW_CURSOR,
                captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
            );
            mouse_position->initialized = false;
        }
    );
    input_api.set_function(
        "mouse_delta",
        [mouse_position]() {
            auto* window = glfwGetCurrentContext();
            if (!window)
                return std::tuple<float, float>{0.f, 0.f};

            double x = 0.0;
            double y = 0.0;
            glfwGetCursorPos(window, &x, &y);
            if (!mouse_position->initialized) {
                mouse_position->x = x;
                mouse_position->y = y;
                mouse_position->initialized = true;
                return std::tuple<float, float>{0.f, 0.f};
            }

            const auto delta = std::tuple<float, float>{
                static_cast<float>(x - mouse_position->x),
                static_cast<float>(y - mouse_position->y)
            };
            mouse_position->x = x;
            mouse_position->y = y;
            return delta;
        }
    );
    bubble["input"] = input_api;

    std::weak_ptr<transform> weak_transform;
    if (reg)
        weak_transform = reg->get<transform>(my_object);
    auto transform_api = estado_lua.create_table();
    transform_api.set_function(
        "get_position",
        [weak_transform]() {
            const auto entity_transform = weak_transform.lock();
            if (!entity_transform)
                return std::tuple<float, float, float>{0.f, 0.f, 0.f};
            const auto position = entity_transform->get_position();
            return std::tuple<float, float, float>{
                position.x,
                position.y,
                position.z
            };
        }
    );
    transform_api.set_function(
        "get_rotation",
        [weak_transform]() {
            const auto entity_transform = weak_transform.lock();
            if (!entity_transform)
                return std::tuple<float, float, float>{0.f, 0.f, 0.f};
            const auto rotation = entity_transform->get_rotation();
            return std::tuple<float, float, float>{
                rotation.x,
                rotation.y,
                rotation.z
            };
        }
    );
    transform_api.set_function(
        "set_rotation",
        [weak_transform](const float x, const float y, const float z) {
            if (const auto entity_transform = weak_transform.lock())
                entity_transform->set_rotation({x, y, z});
        }
    );
    transform_api.set_function(
        "move_forward",
        [weak_transform](const float distance) {
            const auto entity_transform = weak_transform.lock();
            if (!entity_transform)
                return;
            const auto rotation = entity_transform->get_rotation();
            constexpr float degrees_to_radians =
                0.01745329251994329577f;
            const float yaw = rotation.y * degrees_to_radians;
            auto position = entity_transform->get_position();
            position.x += std::cos(yaw) * distance;
            position.z += std::sin(yaw) * distance;
            entity_transform->set_position(position);
        }
    );
    transform_api.set_function(
        "move_right",
        [weak_transform](const float distance) {
            const auto entity_transform = weak_transform.lock();
            if (!entity_transform)
                return;
            const auto rotation = entity_transform->get_rotation();
            constexpr float degrees_to_radians =
                0.01745329251994329577f;
            const float yaw = rotation.y * degrees_to_radians;
            auto position = entity_transform->get_position();
            position.x -= std::sin(yaw) * distance;
            position.z += std::cos(yaw) * distance;
            entity_transform->set_position(position);
        }
    );
    bubble["transform"] = transform_api;

    estado_lua.script_file(arquivoCompleto);

    f_setup = estado_lua["setup"];
    f_update = estado_lua["update"];

    if (f_setup.valid())
        f_setup();
}

bool code::analyze(const rapidjson::Value& value) {
    if (!value.IsObject() ||
        !value.HasMember("directory") ||
        !value["directory"].IsString()) {
        return false;
    }

    file = value["directory"].GetString();
    arquivoCompleto = file;

    estado_lua.open_libraries(sol::lib::base);

    api::setClasses(estado_lua);
    api::setNamespaces(estado_lua);

    return true;
}

bool code::serialize(
    rapidjson::Value& value,
    rapidjson::Document::AllocatorType& allocator
) const {
    rapidjson::Value directory;
    directory.SetString(
        file.c_str(),
        static_cast<rapidjson::SizeType>(file.size()),
        allocator
    );

    value.AddMember("directory", directory, allocator);
    return true;
}

void code::update() {
    if (f_update.valid())
        f_update();
}

code::~code() {
    debugging::emit(debug, "code", "descarregando");
}