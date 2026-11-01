#include <os/resource_files.hpp>
#include <tuple>
#include "components/code.hpp"
#include "debugging/script_log.hpp"

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
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <tuple>
#include <fstream>
#include <functional>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <rapidjson/document.h>

using namespace COMMONS_NS;

namespace {

bool read_property(const rapidjson::Value& entry, const char* value_key, script_property& result) {
    if (!entry.IsObject() || !entry.HasMember("name") || !entry["name"].IsString() ||
        !entry.HasMember("type") || !entry["type"].IsString() || !entry.HasMember(value_key)) return false;
    result.name.assign(entry["name"].GetString(), entry["name"].GetStringLength());
    result.type.assign(entry["type"].GetString(), entry["type"].GetStringLength());
    if (result.name.empty() || !(std::isalpha(static_cast<unsigned char>(result.name[0])) || result.name[0] == '_')) return false;
    for (unsigned char c : result.name) if (!std::isalnum(c) && c != '_') return false;
    const auto& value = entry[value_key];
    if (result.type == "number" && value.IsNumber() && std::isfinite(value.GetDouble()))
        result.value = value.GetDouble();
    else if (result.type == "boolean" && value.IsBool()) result.value = value.GetBool();
    else if ((result.type == "text" || result.type == "image") && value.IsString())
        result.value = std::string(value.GetString(), value.GetStringLength());
    else if (result.type == "color" && value.IsArray() && value.Size() == 4) {
        std::array<float, 4> color;
        for (unsigned i = 0; i < 4; ++i) {
            if (!value[i].IsNumber() || !std::isfinite(value[i].GetDouble()) ||
                value[i].GetDouble() < 0 || value[i].GetDouble() > 1) return false;
            color[i] = value[i].GetFloat();
        }
        result.value = color;
    } else return false;
    return true;
}

bool read_properties(const rapidjson::Value& entries, std::vector<script_property>& result) {
    if (!entries.IsArray()) return false;
    std::set<std::string> names;
    for (const auto& entry : entries.GetArray()) {
        script_property property;
        if (!read_property(entry, "value", property) || !names.insert(property.name).second) return false;
        result.push_back(std::move(property));
    }
    return true;
}

rapidjson::Value write_properties(const std::vector<script_property>& properties,
    rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value entries(rapidjson::kArrayType);
    for (const auto& property : properties) {
        rapidjson::Value entry(rapidjson::kObjectType), value;
        entry.AddMember("name", rapidjson::Value(property.name.c_str(), allocator), allocator);
        entry.AddMember("type", rapidjson::Value(property.type.c_str(), allocator), allocator);
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, double>) value.SetDouble(v);
            else if constexpr (std::is_same_v<T, bool>) value.SetBool(v);
            else if constexpr (std::is_same_v<T, std::string>)
                value.SetString(v.data(), static_cast<rapidjson::SizeType>(v.size()), allocator);
            else {
                value.SetArray();
                for (float channel : v) value.PushBack(channel, allocator);
            }
        }, property.value);
        entry.AddMember("value", value, allocator);
        entries.PushBack(entry, allocator);
    }
    return entries;
}

void bind_script_output(sol::state& lua, std::uint32_t id) {
    lua.set_function("print", [id](sol::variadic_args args, sol::this_state state) {
        sol::state_view view(state);
        sol::function tostring = view["tostring"];
        std::string message;
        bool first = true;
        for (auto argument : args) {
            if (!first) message += '\t';
            first = false;
            message += tostring(argument).get<std::string>();
        }
        script_log::instance().write(id, message);
    });
}

struct cursor_state {
    double x{0.0};
    double y{0.0};
    bool initialized{false};
};

}

code::code(const std::string& file) : file(file) {
    arquivoCompleto = file;

    estado_lua.open_libraries(sol::lib::base);
    if (bgui::resource_files::embedded()) {
        estado_lua.set_function("__bengine_resource_read", [](const std::string& path)
            -> std::tuple<sol::optional<std::string>, sol::optional<std::string>> {
            try { return {bgui::resource_files::text(path), sol::nullopt}; }
            catch (const std::exception& error) { return {sol::nullopt, std::string(error.what())}; }
        });
        estado_lua.script(R"(
            local read_resource, compile = __bengine_resource_read, load
            function loadfile(filename, mode, environment)
                if type(filename) ~= 'string' then return nil, 'Embedded loadfile requires a resource path' end
                local source, message = read_resource(filename)
                if not source then return nil, message end
                if environment == nil then environment = _G end
                return compile(source, '@' .. filename, mode or 'bt', environment)
            end
            function dofile(filename)
                local fn, message = loadfile(filename)
                if not fn then error(message, 2) end
                return fn()
            end
            __bengine_resource_read = nil
        )");
    }


    api::setClasses(estado_lua);
    api::setNamespaces(estado_lua);
    bind_script_output(estado_lua, my_object);
}

void code::setup() {
    bind_script_output(estado_lua, my_object);
    auto bubble = estado_lua["bubble"].get_or_create<sol::table>();
    bubble["meuID"] = my_object;
    bubble.set_function(
        "log",
        [id = my_object](const std::string& message) {
            script_log::instance().write(id, message);
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
            if (key.size() == 1) {
                const auto c = static_cast<unsigned char>(key.front());
                const int upper = std::toupper(c);
                if ((upper >= 'A' && upper <= 'Z') || (upper >= '0' && upper <= '9'))
                    glfw_key = upper;
            } else if (key == "Space" || key == "SPACE" || key == "space")
                glfw_key = GLFW_KEY_SPACE;
            else if (key == "Escape") glfw_key = GLFW_KEY_ESCAPE;
            else if (key == "Return") glfw_key = GLFW_KEY_ENTER;
            else if (key == "Tab") glfw_key = GLFW_KEY_TAB;
            else if (key == "LeftShift") glfw_key = GLFW_KEY_LEFT_SHIFT;
            else if (key == "RightShift") glfw_key = GLFW_KEY_RIGHT_SHIFT;
            else if (key == "LeftControl") glfw_key = GLFW_KEY_LEFT_CONTROL;
            else if (key == "RightControl") glfw_key = GLFW_KEY_RIGHT_CONTROL;
            else if (key == "UpArrow") glfw_key = GLFW_KEY_UP;
            else if (key == "DownArrow") glfw_key = GLFW_KEY_DOWN;
            else if (key == "LeftArrow") glfw_key = GLFW_KEY_LEFT;
            else if (key == "RightArrow") glfw_key = GLFW_KEY_RIGHT;

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
        // GCC can give distinct no-argument lambdas the same sol2 GC metatable name.
        std::function<std::tuple<float, float>()>{[mouse_position]() {
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
        }}
    );
    bubble["input"] = input_api;

    std::weak_ptr<transform> weak_transform;
    if (const auto registry = reg.lock())
        weak_transform = registry->get<transform>(my_object);
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

    api::setUpApi(estado_lua, reg.lock(), my_object);
    std::string property_error;
    if (!load_properties(property_error)) throw sol::error(property_error);
    auto values = estado_lua.create_table();
    for (const auto& property : properties) {
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                values[property.name] = commons::color{value[0], value[1], value[2], value[3]};
            } else if constexpr (std::is_same_v<T, std::string>) {
                if (property.type == "image" && !value.empty() && !asset_root.empty())
                    values[property.name] = (std::filesystem::path(asset_root) / value).lexically_normal().string();
                else values[property.name] = value;
            } else values[property.name] = value;
        }, property.value);
    }
    estado_lua["properties"] = values;
    const auto loaded = estado_lua.safe_script(
        bgui::resource_files::text(arquivoCompleto), sol::script_pass_on_error, "@" + arquivoCompleto);
    if (!loaded.valid()) throw loaded.get<sol::error>();

    f_setup = estado_lua["setup"];
    f_update = estado_lua["update"];

    if (f_setup.valid()) {
        sol::protected_function callback = f_setup;
        const auto result = callback();
        if (!result.valid()) throw result.get<sol::error>();
    }
}

bool code::load_properties(std::string& error) {
    error.clear();
    if (!bgui::resource_files::exists(arquivoCompleto)) { error = "Could not read script: " + arquivoCompleto; return false; }
    auto input = bgui::resource_files::stream(arquivoCompleto);
    if (!input) { error = "Could not read script: " + arquivoCompleto; return false; }
    std::vector<script_property> loaded;
    std::set<std::string> names;
    std::string line;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line.compare(start, 12, "--@property ") != 0) continue;
        rapidjson::Document metadata;
        metadata.Parse(line.c_str() + start + 12);
        script_property property;
        if (metadata.HasParseError() || !read_property(metadata, "default", property) ||
            !names.insert(property.name).second) {
            error = "Invalid Lua property at " + arquivoCompleto + ":" + std::to_string(line_number);
            return false;
        }
        for (const auto& old : properties)
            if (old.name == property.name && old.type == property.type) property.value = old.value;
        loaded.push_back(std::move(property));
    }
    if (input.bad()) { error = "Could not read script: " + arquivoCompleto; return false; }
    properties = std::move(loaded);
    return true;
}

bool code::set_property(const std::string& name, const script_property::value_type& value) {
    for (auto& property : properties) {
        if (property.name != name || property.value.index() != value.index()) continue;
        if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number)) return false;
        if (const auto* color = std::get_if<std::array<float, 4>>(&value))
            for (float channel : *color) if (!std::isfinite(channel) || channel < 0 || channel > 1) return false;
        property.value = value;
        return true;
    }
    return false;
}

bool code::analyze(const rapidjson::Value& value) {
    if (!value.IsObject() ||
        !value.HasMember("directory") ||
        !value["directory"].IsString()) {
        return false;
    }

    std::vector<std::shared_ptr<code>> loaded;
    if (value.HasMember("scripts")) {
        if (!value["scripts"].IsArray()) return false;
        for (const auto& entry : value["scripts"].GetArray()) {
            if (!entry.IsString()) return false;
            loaded.push_back(std::make_shared<code>(entry.GetString()));
        }
    }
    std::vector<script_property> loaded_properties;
    if (value.HasMember("properties") && !read_properties(value["properties"], loaded_properties)) return false;
    if (value.HasMember("script_properties")) {
        const auto& entries = value["script_properties"];
        if (!entries.IsArray() || entries.Size() != loaded.size()) return false;
        for (rapidjson::SizeType i = 0; i < entries.Size(); ++i)
            if (!read_properties(entries[i], loaded[i]->properties)) return false;
    }
    properties = std::move(loaded_properties);
    scripts = std::move(loaded);
    file = value["directory"].GetString();
    arquivoCompleto = file;

    estado_lua.open_libraries(sol::lib::base);

    api::setClasses(estado_lua);
    api::setNamespaces(estado_lua);
    bind_script_output(estado_lua, my_object);

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
    if (!properties.empty()) value.AddMember("properties", write_properties(properties, allocator), allocator);
    if (!scripts.empty()) {
        rapidjson::Value entries(rapidjson::kArrayType);
        for (const auto& script : scripts) {
            rapidjson::Value entry;
            entry.SetString(script->file.c_str(), static_cast<rapidjson::SizeType>(script->file.size()), allocator);
            entries.PushBack(entry, allocator);
        }
        value.AddMember("scripts", entries, allocator);
        rapidjson::Value property_entries(rapidjson::kArrayType);
        for (const auto& script : scripts)
            property_entries.PushBack(write_properties(script->properties, allocator), allocator);
        value.AddMember("script_properties", property_entries, allocator);
    }
    return true;
}

void code::update() {
    if (f_update.valid()) {
        sol::protected_function callback = f_update;
        const auto result = callback();
        if (!result.valid()) throw result.get<sol::error>();
    }
}

code::~code() {
    debugging::emit(debug, "code", "descarregando");
}
