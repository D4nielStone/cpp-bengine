#include "live_scene.hpp"

#include "editor_context.hpp"
#include "project_persistence_system.hpp"
#include "project_file_transaction.hpp"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <stdexcept>

namespace editor::ui {
namespace {
using rapidjson::Document;
using rapidjson::Value;

std::string encode(const Value& value) {
    rapidjson::StringBuffer bytes;
    rapidjson::Writer writer(bytes);
    value.Accept(writer);
    return {bytes.GetString(), bytes.GetSize()};
}

const Value& field(const Value& value, const char* name) {
    static const Value missing;
    return value.IsObject() && value.HasMember(name) ? value[name] : missing;
}

std::string_view string(const Value& value) {
    if (!value.IsString()) return {};
    return {value.GetString(), value.GetStringLength()};
}

bool matches_token(std::string_view supplied, std::string_view expected) {
    if (supplied.size() != expected.size()) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) difference |= supplied[i] ^ expected[i];
    return difference == 0;
}

std::filesystem::path live_path(const std::filesystem::path& root, std::string_view name) {
    const std::filesystem::path relative(name);
    if (name.empty() || name.find('\0') != std::string_view::npos ||
        relative.has_root_path()) throw std::runtime_error("Use a project-relative path.");
    auto path = root;
    for (const auto& part : relative) {
        if (part.empty() || part == "." || part == "..")
            throw std::runtime_error("Use a project-relative path.");
        path /= part;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
            throw std::runtime_error("Symlinks are not allowed.");
    }
    return path;
}

void write_asset(const Value& message, const std::filesystem::path& target,
    const std::filesystem::path& active_scene) {
    const auto ext = target.extension();
    if (target == active_scene)
        throw std::runtime_error("Edit the active scene through its live editor transaction.");
    if (ext != ".scene" && ext != ".world" && ext != ".vert" && ext != ".frag" &&
        ext != ".bmodel" && ext != ".banim" && ext != ".lua")
        throw std::runtime_error("Unsupported asset type.");
    const auto& contents = field(message, "contents");
    const auto bytes = string(contents);
    if (!contents.IsString() || bytes.size() > 400000 || bytes.find('\0') != std::string_view::npos)
        throw std::runtime_error("Expected an asset of at most 400000 text bytes.");
    if (std::filesystem::exists(project_files::journal_path(target)))
        throw std::runtime_error("Recover the pending asset transaction before editing.");
    const auto& expected = field(message, "expectedContents");
    if (std::filesystem::exists(target)) {
        if (!expected.IsString() || !std::filesystem::is_regular_file(target) ||
            std::filesystem::file_size(target) != expected.GetStringLength() ||
            project_files::read(target) != string(expected))
            throw std::runtime_error("Asset changed. Read it again and retry.");
    } else if (!expected.IsNull()) {
        throw std::runtime_error("Asset no longer exists.");
    }
    std::filesystem::create_directories(target.parent_path());
    std::string error;
    if (!project_files::save(target, {{target, std::string(bytes)}}, error))
        throw std::runtime_error(error);
}

struct scene_state {
    decltype(commons::ecs::entities) entities;
    decltype(commons::ecs::mascaras) masks;
    decltype(commons::ecs::live_entities) live;
    decltype(commons::ecs::entity_names) registry_names;
    project_persistence_system::entity_name_map names;
    project_persistence_system::entity_state_set hidden, locked;

    explicit scene_state(const editor_context& editor)
        : entities(editor.registry->entities), masks(editor.registry->mascaras),
          live(editor.registry->live_entities), registry_names(editor.registry->entity_names),
          names(editor.entity_names), hidden(editor.hidden_entities), locked(editor.locked_entities) {}

    void restore(editor_context& editor) const {
        editor.registry->entities = entities;
        editor.registry->mascaras = masks;
        editor.registry->live_entities = live;
        editor.registry->entity_names = registry_names;
        editor.entity_names = names;
        editor.hidden_entities = hidden;
        editor.locked_entities = locked;
        editor.synchronize_selection();
        ++editor.history_revision;
    }
};

void replace_scene(const Value& current, const Value& replacement_scene, editor_context& context,
    project_persistence_system& persistence, const std::string& project) {
    Document replacement;
    replacement.CopyFrom(replacement_scene, replacement.GetAllocator());
    std::string error;
    if (!replacement.IsObject() || !validate_scene_entities(field(replacement, "entities"), error))
        throw std::runtime_error("Invalid replacement scene: " + error);
    std::unordered_map<std::uint32_t, const Value*> updated;
    for (const auto& entity : replacement["entities"].GetArray()) {
        const auto id = entity["id"].GetUint();
        if (id == context.editor_camera_entity) throw std::runtime_error("The editor camera is reserved.");
        updated.emplace(id, &entity);
    }

    for (const auto& entity : current["entities"].GetArray()) {
        const auto id = entity["id"].GetUint();
        const auto found = updated.find(id);
        if ((found == updated.end() || *found->second != entity) && context.locked_entities.contains(id))
            throw std::runtime_error("Unlock the entity in the editor before editing it.");
    }
    if (current == replacement) return;
    const scene_state before(context);
    if (!persistence.apply_live_scene(project, replacement, current, error))
        throw std::runtime_error(error);
    ++context.history_revision;
    context.synchronize_selection();
    const scene_state after(context);
    // History is owned by this context and cleared on project changes.
    context.record_action([&context, before] { before.restore(context); },
        [&context, after] { after.restore(context); });
}
}

std::string dispatch_live_scene(std::string_view request, std::string_view token,
    const std::string& project, editor_context& context, project_persistence_system& persistence,
    const live_document_hooks& documents) {
    Document result(rapidjson::kObjectType);
    auto& memory = result.GetAllocator();
    try {
        Document message;
        message.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(request.data(), request.size());
        if (message.HasParseError() || !matches_token(string(field(message, "token")), token))
            throw std::runtime_error("Invalid live editor credential.");
        const auto action = string(field(message, "action"));
        const auto active = std::filesystem::canonical(project);
        const auto check_document = [&](const auto& path) {
            if (documents.dirty && documents.dirty(path))
                throw std::runtime_error("Save the pending text edits in the editor before continuing.");
        };
        const auto refresh_document = [&](const auto& path) {
            if (documents.reload) {
                try { documents.reload(path); }
                catch (const std::exception&) { context.events.publish(editor_event::assets); }
            }
        };
        if (action == "snapshot") {
            Document manifest, scene;
            std::string error;
            if (!persistence.live_documents(project, manifest, scene, error)) throw std::runtime_error(error);
            result.AddMember("manifest", Value(manifest, memory), memory);
            result.AddMember("scene", Value(scene, memory), memory);
        } else {
            if (string(field(message, "project")) != active.string())
                throw std::runtime_error("The editor switched projects. Read the active project and retry.");
            if (context.is_playing) throw std::runtime_error("Stop the in-editor play session before editing.");
            if (action == "replace_scene") {
                check_document(persistence.active_scene());
                Document manifest, scene;
                std::string error;
                if (!persistence.live_documents(project, manifest, scene, error)) throw std::runtime_error(error);
                const auto& expected = field(message, "expected");
                if (string(field(message, "path")) != string(manifest["scene"]) || expected != scene)
                    throw std::runtime_error("The live scene changed. Read it again and retry.");
                replace_scene(scene, field(message, "scene"), context, persistence, project);
            } else if (action == "write_asset") {
                const auto target = live_path(active.parent_path(), string(field(message, "path")));
                check_document(target);
                write_asset(message, target, std::filesystem::weakly_canonical(persistence.active_scene()));
                refresh_document(target);
                context.events.publish(editor_event::assets);
            } else if (action == "switch_scene") {
                Document manifest, scene;
                std::string error;
                if (!persistence.live_documents(project, manifest, scene, error)) throw std::runtime_error(error);
                if (field(message, "expected") != manifest)
                    throw std::runtime_error("The live project changed. Read it again and retry.");
                const auto path = live_path(active.parent_path(), string(field(message, "path")));
                check_document(active);
                check_document(persistence.active_scene());
                check_document(path);
                if (!persistence.switch_scene(project, path, error)) throw std::runtime_error(error);
                context.animation_assets.clear();
                context.clear_history();
                context.select_entity(std::nullopt);
                context.events.publish(editor_event::scene);
                refresh_document(active);
            } else if (action == "save") {
                check_document(active);
                check_document(persistence.active_scene());
                std::string error;
                if (!persistence.save(project, error)) throw std::runtime_error(error);
                refresh_document(active);
                refresh_document(persistence.active_scene());
            } else if (action == "undo" || action == "redo") {
                const bool applied = action == "undo" ? context.undo() : context.redo();
                result.AddMember("applied", applied, memory);
            } else {
                throw std::runtime_error("Unknown live editor action.");
            }
        }
        const auto file = active.string();
        result.AddMember("project", Value(file.c_str(), memory), memory);
    } catch (const std::exception& error) {
        result.SetObject();
        result.AddMember("error", Value(error.what(), memory), memory);
    }
    return encode(result);
}

}
