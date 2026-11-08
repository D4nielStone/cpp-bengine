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
    // Keep byte offsets while masking comments and all Lua string forms.
    std::string code(source);
    bool inside = false;
    for (std::size_t i = 0; i < source.size();) {
        const auto begin = i;
        bool comment = source.substr(i, 2) == "--";
        if (comment) i += 2;
        std::size_t bracket = i;
        if (i < source.size() && source[i] == '[') {
            auto end = i + 1;
            while (end < source.size() && source[end] == '=') ++end;
            if (end < source.size() && source[end] == '[') {
                const auto closing = "]" + std::string(end - i - 1, '=') + "]";
                const auto close = source.find(closing, end + 1);
                i = close == std::string_view::npos ? source.size() : close + closing.size();
                if (cursor > begin && (close == std::string_view::npos || cursor < i)) inside = true;
            }
        }
        if (i == bracket) {
            if (comment) {
                const auto end = source.find('\n', i);
                i = end == std::string_view::npos ? source.size() : end;
                if (cursor > begin && cursor <= i) inside = true;
            } else if (i < source.size() && (source[i] == '\'' || source[i] == '"')) {
                const auto quote = source[i++];
                bool closed = false;
                while (i < source.size()) {
                    if (source[i] == '\\') i = std::min(i + 2, source.size());
                    else if (source[i++] == quote) { closed = true; break; }
                }
                if (cursor > begin && (cursor < i || (!closed && cursor == i))) inside = true;
            } else { ++i; continue; }
        }
        for (auto j = begin; j < i; ++j) if (code[j] != '\n') code[j] = ' ';
    }
    if (inside) return std::nullopt;
    auto identifier = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    auto start = cursor;
    while (start && identifier(source[start - 1])) --start;
    const bool member = start && (source[start - 1] == '.' || source[start - 1] == ':');
    const bool methods = member && source[start - 1] == ':';
    auto object_start = member ? start - 1 : start;
    if (member)
        while (object_start && (identifier(source[object_start - 1]) || source[object_start - 1] == '.'))
            --object_start;
    const std::string object(member ? source.substr(object_start, start - 1 - object_start) : "");
    if (!member && (start == cursor || (start && identifier(source[start - 1])))) return std::nullopt;
    std::string type = object;
    if (object == "gameObject.transform") type = "transform";
    // Infer common local aliases, without executing user code.
    const std::string before(source.substr(0, object_start));
    if (!object.empty() && std::all_of(object.begin(), object.end(), identifier)) {
        const std::regex assignment("\\b" + object + R"(\s*=\s*([^\n;]+))");
        for (auto it = std::sregex_iterator(before.begin(), before.end(), assignment);
             it != std::sregex_iterator(); ++it) {
            if (code[static_cast<std::size_t>((*it).position())] == ' ') continue;
            auto value = (*it)[1].str();
            value.erase(value.find_last_not_of(" \t\r") + 1);
            if (const auto comment = value.find("--"); comment != std::string::npos)
                value.erase(comment);
            value.erase(value.find_last_not_of(" \t\r") + 1);
            if (value.find("GetComponent") != std::string::npos &&
                (value.find("\"Transform\"") != std::string::npos || value.find("'Transform'") != std::string::npos))
                type = "transform";
            else if (value.find("GetComponent") != std::string::npos &&
                (value.find("\"Renderer\"") != std::string::npos || value.find("'Renderer'") != std::string::npos))
                type = "renderer";
            else if (value.find("GetComponent") != std::string::npos && value.find("Rigidbody") != std::string::npos) type = "rigidbody";
            else if (value.find("GetComponent") != std::string::npos && value.find("DirectionalLight") != std::string::npos) type = "directionalLight";
            else if (value.find("GetComponent") != std::string::npos && value.find("PointLight") != std::string::npos) type = "pointLight";
            else if (value.find("GetComponent") != std::string::npos && value.find("SpotLight") != std::string::npos) type = "spotLight";
            else if (value.find("GetComponent") != std::string::npos && value.find("SoundListener") != std::string::npos) type = "soundListener";
            else if (value.find("GetComponent") != std::string::npos && value.find("SoundPlayer") != std::string::npos) type = "soundPlayer";
            else if (value.find("GetComponent") != std::string::npos && value.find("Camera") != std::string::npos) type = "camera";
            else if (value.find("GetComponent") != std::string::npos && value.find("Pathfinder") != std::string::npos) type = "pathfinder";
            else if (value.find("GetComponent") != std::string::npos && value.find("Animation") != std::string::npos) type = "animation";
            else if (value.find("GetComponent") != std::string::npos && value.find("HUD") != std::string::npos) type = "hud";
            else if (value.find(":AddHUD(") != std::string::npos) type = "hud";
            else if (std::regex_search(value, std::regex(R"(^[A-Za-z_][A-Za-z0-9_]*:(Add|Get)\()"))) {
                const auto receiver = value.substr(0, value.find(':'));
                const std::regex hud_assignment("\\b" + receiver + R"(\s*=\s*[^\n;]*(AddHUD\(|GetComponent\(["']HUD["']))");
                type = std::regex_search(before, hud_assignment) ? "hudElement" : "";
            }
            else if (value.starts_with("Color(")) type = "color";
            else if (value.starts_with("Vector3(")) type = "vector3";
            else if (value.starts_with("gameObject.transform") || value == "transform") type = "transform";
            else if (value.starts_with("GameObject.Find(") || value == "gameObject") type = "gameObject";
            else type.clear();
        }
    }
    std::vector<std::string> names;
    if (!member) {
        names = {"Color", "Debug", "Destroy", "GameObject", "Input", "SceneManager", "Time", "Vector3", "bubble",
            "gameObject", "transform", "print", "setup", "update"};
    } else if (type == "SceneManager" && !methods) {
        names = {"LoadScene", "ReloadScene", "GetActiveScene", "GetLastError", "IsLoading"};
    } else if (type == "pathfinder") {
        names = {"Configure", "FindPath", "GetPath", "Clear"};
    } else if (type == "animation") {
        names = {"Play", "Pause", "Stop", "Seek", "CrossFade", "SetSpeed"};
        if (!methods) names.insert(names.end(), {"time", "isPlaying", "isBlending", "speed"});
    } else if (type == "hud") {
        names = {"Add", "Get"};
        if (!methods) names.insert(names.end(), {"enabled", "count"});
    } else if (type == "hudElement") {
        names = {"ConsumeClick", "SetScreenPosition"};
        if (!methods) names.insert(names.end(), {"text", "visible", "width", "height", "scale", "position", "absolute"});
    } else if (type == "color" && !methods) {
        names = {"r", "g", "b", "a"};
    } else if (type == "transform") {
        names = {"Rotate", "Translate"};
        if (!methods) names.insert(names.end(), {"eulerAngles", "localEulerAngles", "localPosition", "localScale", "position"});
    } else if (type == "vector3" || (type == "Vector3" && !methods)) {
        names = {"cross", "distance", "dot", "length", "lerp", "normalized"};
        if (type == "vector3" && !methods) names.insert(names.end(), {"x", "y", "z"});
    } else if (type == "gameObject") {
        names = {"AddHUD", "GetComponent", "IsCollidingWith", "IsDescendantOf", "IsOverlappingTrigger", "SetParent"};
        if (!methods) names.insert(names.end(), {"id", "transform"});
    } else if (type == "camera") {
        names = {"screenToWorld", "traceRay", "worldToScreen"};
        if (!methods) names.insert(names.end(), {"forward", "fieldOfView", "aspect", "nearClipPlane",
            "farClipPlane", "orthographic", "orthographicSize", "backgroundColor", "useSkybox",
            "viewportWidth", "viewportHeight", "skyboxImage", "fogEnabled", "fogColor", "fogDensity"});
    } else if (type == "rigidbody") {
        names = {"AddForce", "SetLinearFactor", "SetAngularFactor"};
        if (!methods) names.insert(names.end(), {"mass", "friction", "restitution", "velocity"});
    } else if (type == "spotLight" && methods) {
        names = {"GetWorldDirection", "SetInnerCutoff", "SetOuterCutoff"};
    } else if (!methods) {
        if (type == "directionalLight") names = {"direction", "ambient", "color", "intensity", "shadows", "shadowDistance", "shadowBias", "shadowStrength"};
        else if (type == "pointLight") names = {"position", "color", "intensity", "constant", "linear", "quadratic"};
        else if (type == "spotLight") names = {"position", "worldPosition", "direction", "worldDirection", "color", "intensity", "constant", "linear", "quadratic", "innerCutoff", "outerCutoff", "shadows", "shadowDistance", "shadowBias", "shadowStrength"};
        else if (type == "soundListener") names = {"enabled"};
        else if (type == "soundPlayer") names = {"clip", "volume", "pitch", "radius", "looping", "playOnAwake"};
        else if (type == "renderer") names = {"enabled", "visible", "castShadows", "receiveShadows"};
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
