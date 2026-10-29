#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace editor::ui {
struct lua_completion {
    std::size_t begin{};
    std::size_t end{};
    std::vector<std::string> members;
};

// A deliberately small catalog of the engine API, not a Lua language server.
inline std::optional<lua_completion> complete_lua(std::string_view source, std::size_t cursor) {
    if (cursor > source.size()) return std::nullopt;
    // Ignore member-like text inside comments and quoted/long strings.
    char quote = 0;
    bool comment = false, long_string = false;
    for (std::size_t i = 0; i < cursor; ++i) {
        const char c = source[i];
        if (long_string) {
            if (c == ']' && i + 1 < cursor && source[i + 1] == ']') {
                long_string = false; ++i;
            }
        } else if (comment) {
            if (c == '\n') comment = false;
        } else if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
        } else if (c == '-' && i + 1 < cursor && source[i + 1] == '-') {
            if (i + 3 < cursor && source.substr(i + 2, 2) == "[[") {
                long_string = true; i += 3;
            } else { comment = true; ++i; }
        } else if (c == '[' && i + 1 < cursor && source[i + 1] == '[') {
            long_string = true; ++i;
        } else if (c == '\'' || c == '"') quote = c;
    }
    if (quote || comment || long_string) return std::nullopt;
    auto identifier = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    auto start = cursor;
    while (start && identifier(source[start - 1])) --start;
    if (!start || (source[start - 1] != '.' && source[start - 1] != ':')) return std::nullopt;
    const bool methods = source[start - 1] == ':';
    auto object_start = start - 1;
    while (object_start && (identifier(source[object_start - 1]) || source[object_start - 1] == '.'))
        --object_start;
    const std::string object(source.substr(object_start, start - 1 - object_start));
    std::string type = object;
    if (object == "gameObject.transform") type = "transform";
    // Infer common local aliases, without executing user code.
    const std::string before(source.substr(0, object_start));
    if (!object.empty() && std::all_of(object.begin(), object.end(), identifier)) {
        const std::regex assignment("\\b" + object + R"(\s*=\s*([^\n;]+))");
        for (auto it = std::sregex_iterator(before.begin(), before.end(), assignment);
             it != std::sregex_iterator(); ++it) {
            auto value = (*it)[1].str();
            if (value.find("GetComponent") != std::string::npos &&
                (value.find("\"Transform\"") != std::string::npos || value.find("'Transform'") != std::string::npos))
                type = "transform";
            else if (value.find("GetComponent") != std::string::npos &&
                (value.find("\"Renderer\"") != std::string::npos || value.find("'Renderer'") != std::string::npos))
                type = "renderer";
            else if (value.find("GetComponent") != std::string::npos && value.find("Rigidbody") != std::string::npos) type = "rigidbody";
            else if (value.find("GetComponent") != std::string::npos && value.find("DirectionalLight") != std::string::npos) type = "directionalLight";
            else if (value.find("GetComponent") != std::string::npos && value.find("PointLight") != std::string::npos) type = "pointLight";
            else if (value.find("GetComponent") != std::string::npos && value.find("Camera") != std::string::npos) type = "camera";
            else if (value.starts_with("gameObject.transform") || value == "transform") type = "transform";
            else if (value.starts_with("GameObject.Find(") || value == "gameObject") type = "gameObject";
            else type.clear();
        }
    }
    std::vector<std::string> names;
    if (type == "transform") {
        names = {"Rotate", "Translate"};
        if (!methods) names.insert(names.end(), {"eulerAngles", "localEulerAngles", "localPosition", "localScale", "position"});
    } else if (type == "gameObject") {
        names = {"GetComponent"};
        if (!methods) names.insert(names.end(), {"id", "transform"});
    } else if (type == "rigidbody") {
        names = {"AddForce", "SetLinearFactor", "SetAngularFactor"};
        if (!methods) names.insert(names.end(), {"mass", "friction", "restitution", "velocity"});
    } else if (!methods) {
        if (type == "camera") names = {"fieldOfView", "aspect", "nearClipPlane", "farClipPlane", "orthographic", "orthographicSize", "backgroundColor", "useSkybox"};
        else if (type == "directionalLight") names = {"direction", "ambient", "color", "intensity"};
        else if (type == "pointLight") names = {"position", "color", "intensity", "constant", "linear", "quadratic"};
        else if (type == "renderer") names = {"enabled", "visible"};
        else if (type == "GameObject") names = {"Find"};
        else if (type == "Time") names = {"deltaTime", "time"};
        else if (type == "Input") names = {"GetAxis", "GetKey", "GetMouseDelta", "SetMouseCaptured"};
        else if (type == "Debug") names = {"Log"};
        else if (type == "bubble") names = {"delta_time", "entity", "input", "log", "meuID", "transform"};
        else if (type == "bubble.transform") names = {"get_position", "get_rotation", "move_forward", "move_right", "set_rotation"};
        else if (type == "bubble.input") names = {"is_key_down", "mouse_delta", "set_mouse_captured"};
        else if (type == "bubble.entity") names = {"get_position", "remove_physics", "set_visible"};
    }
    const auto prefix = source.substr(start, cursor - start);
    names.erase(std::remove_if(names.begin(), names.end(), [&](const auto& name) {
        return !std::string_view(name).starts_with(prefix);
    }), names.end());
    if (names.empty()) return std::nullopt;
    std::sort(names.begin(), names.end());
    auto end = cursor;
    while (end < source.size() && identifier(source[end])) ++end;
    return lua_completion{start, end, std::move(names)};
}
}
