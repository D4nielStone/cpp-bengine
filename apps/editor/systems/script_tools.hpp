#pragma once

#include "../core/editor_context.hpp"
#include "components/code.hpp"
#include "components/transform.hpp"
#include "script_templates.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <optional>

namespace editor::ui {
inline bool is_lua_script(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

inline bool create_lua_script(const std::filesystem::path& directory,
    std::string name, std::filesystem::path& created, std::string& error,
    lua_script_preset preset = lua_script_preset::fpp_controller) {
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
    stream << script_template(preset).source;
    stream.close();
    if (!stream) {
        error = "Could not create the script.";
        return false;
    }
    created = path;
    return true;
}

inline bool attach_lua_script(const std::shared_ptr<editor_context>& context,
    editor_context::entity_id id, const std::filesystem::path& requested, std::string& error, std::optional<std::size_t> replace = std::nullopt) {
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
            error = result.get<sol::error>().what();
            return false;
        }
        if (!next->load_properties(error)) return false;
        std::error_code relative_error;
        const auto relative = std::filesystem::relative(path, context->project_root, relative_error);
        next->file = !relative_error && !relative.empty() && *relative.begin() != ".."
            ? relative.generic_string() : path.string();
        next->arquivoCompleto = path.string();
        next->asset_root = context->project_root.string();
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
        if (previous && replace == 0) {
            next->scripts = previous->scripts;
        } else if (previous) {
            auto before = previous->scripts;
            auto after = before;
            if (replace) {
                if (*replace > after.size()) { error = "Script no longer exists."; return false; }
                after[*replace - 1] = next;
            } else after.push_back(next);
            previous->scripts = after;
            context->record_action([previous, before]() { previous->scripts = before; },
                [previous, after]() { previous->scripts = after; });
            return true;
        }
        apply(next);
        context->record_action([apply, previous]() { apply(previous); },
            [apply, next]() { apply(next); });
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}
inline bool remove_lua_script(const std::shared_ptr<editor_context>& context,
    editor_context::entity_id id, std::size_t index, std::string& error) {
    error.clear();
    if(!context || !context->registry || !context->registry->is_alive(id) || context->is_playing ||
        context->locked_entities.contains(id) || context->editor_camera_entity==id) {
        error="Escolha um objeto desbloqueado durante a edição."; return false;
    }
    const auto registry=context->registry;
    const auto script=registry->get<commons::code>(id);
    if(!script || index>script->scripts.size()) {error="O script não está mais associado ao objeto.";return false;}
    if(index) {
        const auto before=script->scripts;
        auto after=before; after.erase(after.begin()+index-1);
        auto apply=[registry,id,script](const auto& entries) {
            if(registry->is_alive(id) && registry->get<commons::code>(id)==script) script->scripts=entries;
        };
        apply(after);
        context->record_action([apply,before]{apply(before);},[apply,after]{apply(after);});
    } else {
        std::shared_ptr<commons::code> next;
        if(!script->scripts.empty()) {
            const auto& first=script->scripts.front();
            next=std::make_shared<commons::code>(first->file);
            next->arquivoCompleto=first->arquivoCompleto; next->properties=first->properties;
            next->asset_root=first->asset_root; next->reg=registry; next->my_object=id;
            next->scripts.assign(script->scripts.begin()+1,script->scripts.end());
        }
        const auto identity=registry->get<commons::transform>(id);
        auto apply=[registry,id,identity](const auto& expected,const auto& value) {
            if(!registry->is_alive(id) || registry->get<commons::transform>(id)!=identity || registry->get<commons::code>(id)!=expected) return;
            if(value) {
                registry->entities[id][commons::code::mask]=value;
                registry->mascaras[id]|=commons::code::mask;
            } else registry->remove<commons::code>(id);
        };
        apply(script,next);
        context->record_action([apply,script,next]{apply(next,script);},[apply,script,next]{apply(script,next);});
    }
    return true;
}

}
