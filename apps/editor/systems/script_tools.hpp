#pragma once

#include "../core/editor_context.hpp"
#include "components/code.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace editor::ui {
inline bool is_lua_script(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

inline bool create_lua_script(const std::filesystem::path& directory,
    std::string name, std::filesystem::path& created, std::string& error) {
    error.clear();
    created.clear();
    if (name.empty() || name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
        name == "." || name == ".." ||
        std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); })) {
        error = "Choose a file name inside the current folder.";
        return false;
    }
    std::filesystem::path filename(name);
    if (!filename.has_extension()) filename += ".lua";
    if (!is_lua_script(filename)) {
        error = "Scripts must use the .lua extension.";
        return false;
    }
    const auto path = directory / filename;
    std::error_code ec;
    if (std::filesystem::exists(path, ec) || ec) {
        error = "This file already exists or cannot be accessed.";
        return false;
    }
    std::ofstream stream(path, std::ios::binary);
    stream << "-- Called once when the scene starts.\nfunction setup()\nend\n\n"
              "-- Called every frame.\nfunction update()\nend\n";
    stream.close();
    if (!stream) {
        error = "Could not create the script.";
        return false;
    }
    created = path;
    return true;
}

inline bool attach_lua_script(const std::shared_ptr<editor_context>& context,
    editor_context::entity_id id, const std::filesystem::path& requested, std::string& error) {
    error.clear();
    if (!context || !context->registry || !context->registry->is_alive(id) ||
        context->is_playing || context->locked_entities.contains(id) ||
        context->editor_camera_entity == id) {
        error = "Choose an unlocked scene entity while editing.";
        return false;
    }
    if (!is_lua_script(requested)) {
        error = "Choose a Lua script.";
        return false;
    }
    try {
        const auto path = std::filesystem::absolute(requested.is_absolute()
            ? requested : context->project_root / requested).lexically_normal();
        // Compile without executing: editing a component must not run user code.
        auto next = std::make_shared<commons::code>(path.string());
        auto result = next->estado_lua.load_file(path.string());
        if (!result.valid()) {
            error = sol::error(result).what();
            return false;
        }
        const auto relative = path.lexically_relative(context->project_root);
        next->file = !relative.empty() && *relative.begin() != ".."
            ? relative.generic_string() : path.string();
        next->arquivoCompleto = path.string();
        next->reg = context->registry;
        next->my_object = id;
        auto registry = context->registry;
        auto previous = registry->get<commons::code>(id);
        auto apply = [registry, id](const std::shared_ptr<commons::code>& value) {
            if (!registry->is_alive(id)) return;
            if (value) {
                registry->entities[id][commons::code::mask] = value;
                registry->mascaras[id] |= commons::code::mask;
            } else registry->remove<commons::code>(id);
        };
        apply(next);
        context->record_action([apply, previous]() { apply(previous); },
            [apply, next]() { apply(next); });
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
}
