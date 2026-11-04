#include "components/hud.hpp"
#include "project_migration_manager.hpp"
#include <os/resource_files.hpp>
#include "project_persistence_system.hpp"
#include "project_file_transaction.hpp"
#include "loaders/material_asset.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/directional_light.hpp"
#include "components/physics.hpp"
#include "components/renderer.hpp"
#include "components/sound_listener.hpp"
#include "components/sound_player.hpp"
#include "components/spot_light.hpp"
#include "components/transform.hpp"
#include "entities/entity.hpp"
#include "util/pbr_material.hpp"

#include <rapidjson/document.h>

#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <optional>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace editor::ui {
namespace {

using component_mask = commons::component::mask;

std::string saved_state_key(const std::string& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? std::filesystem::path(path) : absolute).lexically_normal().string();
}

bool is_number(const rapidjson::Value& value) {
    return value.IsNumber();
}

bool is_vector3(const rapidjson::Value& value) {
    if (!value.IsArray() || value.Size() != 3)
        return false;
    for (const auto& item : value.GetArray())
        if (!is_number(item))
            return false;
    return true;
}

bool is_color4(const rapidjson::Value& value) {
    if (!value.IsArray() || value.Size() != 4)
        return false;
    for (const auto& channel : value.GetArray())
        if (!is_number(channel))
            return false;
    return true;
}

bool is_scene_path(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return extension == ".scene" || extension == ".world";
}

commons::color material_albedo(const commons::material& material) {
    const auto albedo = material.getUniforme("material.albedo");
    if (albedo.m_type == commons::uniform::type::Cor)
        return albedo.valor.color_value;
    if (albedo.m_type == commons::uniform::type::Cor_ptr &&
        albedo.ponteiro.color_ptr)
        return *albedo.ponteiro.color_ptr;
    return {};
}

bool validate_component(const std::string& name, const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;

    if (name == "transform") {
        return value.HasMember("position") && is_vector3(value["position"]) &&
               value.HasMember("rotation") && is_vector3(value["rotation"]) &&
               value.HasMember("scale") && is_vector3(value["scale"]);
    }

    if (name == "renderer") {
        if (value.HasMember("model") && !value["model"].IsString())
            return false;
        for (const auto* key : {"cast_shadows", "receive_shadows"})
            if (value.HasMember(key) && !value[key].IsBool()) return false;
        if (value.HasMember("visible") && !value["visible"].IsBool())
            return false;
        if (!value.HasMember("model") && !value.HasMember("visible"))
            return false;
        if (!value.HasMember("materials"))
            return true;
        if (!value["materials"].IsArray())
            return false;
        for (const auto& material : value["materials"].GetArray()) {
            if (material.IsObject() && material.HasMember("file") &&
                (!material["file"].IsString() || material["file"].GetStringLength()==0 ||
                 material["file"].GetStringLength()!=std::strlen(material["file"].GetString()))) return false;
            if (!material.IsObject() || !material.HasMember("albedo") ||
                !is_color4(material["albedo"]))
                return false;
            for (const auto& factor : commons::pbr_factors) {
                if (material.HasMember(factor.name) &&
                    (!material[factor.name].IsNumber() ||
                     !(material[factor.name].GetDouble() >= 0.0 &&
                       material[factor.name].GetDouble() <= 1.0)))
                    return false;
            }
            if (material.HasMember("world_texture") && !material["world_texture"].IsBool())
                return false;
            for (const auto& factor : commons::texture_factors)
                if (material.HasMember(factor.name) && !is_number(material[factor.name]))
                    return false;
            if (material.HasMember("maps")) {
                if (!material["maps"].IsObject())
                    return false;
                for (const auto& map : commons::pbr_maps)
                    if (material["maps"].HasMember(map.name) &&
                        !material["maps"][map.name].IsString())
                        return false;
            }
        }
        return true;
    }

    if (name == "code")
        return value.HasMember("directory") && value["directory"].IsString();

    if (name == "physics") {
        if (!value.HasMember("shape") || !value["shape"].IsString())
            return false;
        const std::string shape(value["shape"].GetString());
        if (shape != "box" && shape != "capsule" && shape != "sphere" &&
            shape != "cylinder" && shape != "cone" && shape != "plane" &&
            shape != "mesh")
            return false;
        if ((value.HasMember("static") && !value["static"].IsBool()) ||
            (value.HasMember("mass") &&
                (!is_number(value["mass"]) || value["mass"].GetFloat() < 0.f)) ||
            (value.HasMember("friction") && !is_number(value["friction"])) ||
            (value.HasMember("restitution") &&
                !is_number(value["restitution"])))
            return false;
        if (value.HasMember("model_collider") &&
            (!value["model_collider"].IsString() ||
                (std::string(value["model_collider"].GetString()) != "convex" &&
                 std::string(value["model_collider"].GetString()) != "concave")))
            return false;
        return true;
    }

    if (name == "camera") {
        commons::camera camera;
        return camera.analyze(value);
    }

    if (name == "directional_light") {
        return value.HasMember("direction") && is_vector3(value["direction"]) &&
               value.HasMember("ambient") && is_vector3(value["ambient"]) &&
               value.HasMember("color") && is_vector3(value["color"]) &&
               value.HasMember("intensity") && is_number(value["intensity"]);
    }

    if (name == "spot_light") {
        commons::spot_light light;
        return light.analyze(value);
    }

    if (name == "sound_listener") {
        commons::sound_listener listener;
        return listener.analyze(value);
    }

    if (name == "hud") { commons::hud h; return h.analyze(value); }

    if (name == "sound_player") {
        commons::sound_player player;
        return player.analyze(value);
    }

    return false;
}

std::string component_name(const component_mask mask) {
    switch (mask) {
    case commons::component::COMPONENTE_TRANSFORMACAO:
        return "transform";
    case commons::component::COMPONENTE_RENDER:
        return "renderer";
    case commons::component::COMPONENTE_CAM:
        return "camera";
    case commons::component::COMPONENTE_CODIGO:
        return "code";
    case commons::component::COMPONENTE_FISICA:
        return "physics";
    case commons::component::COMPONENTE_LUZ_DIRECIONAL:
        return "directional_light";
    case commons::component::COMPONENTE_LUZ_HOLOFOTE:
        return "spot_light";
    case commons::component::COMPONENTE_SOUND_LISTENER:
        return "sound_listener";
    case commons::component::COMPONENTE_HUD: return "hud";
    case commons::component::COMPONENTE_SOUND_PLAYER:
        return "sound_player";
    default:
        return {};
    }
}

bool serialize_component(
    const std::shared_ptr<commons::component>& component,
    const component_mask mask,
    rapidjson::Value& destination,
    rapidjson::Document::AllocatorType& allocator
) {
    if (!component)
        return false;

    switch (mask) {
    case commons::component::COMPONENTE_TRANSFORMACAO:
        return std::static_pointer_cast<commons::transform>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_RENDER:
        return std::static_pointer_cast<commons::renderer>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_CAM:
        return std::static_pointer_cast<commons::camera>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_CODIGO:
        return std::static_pointer_cast<commons::code>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_FISICA:
        return std::static_pointer_cast<commons::physics>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_LUZ_DIRECIONAL:
        return std::static_pointer_cast<commons::directional_light>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_LUZ_HOLOFOTE:
        return std::static_pointer_cast<commons::spot_light>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_SOUND_LISTENER:
        return std::static_pointer_cast<commons::sound_listener>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_HUD: return std::static_pointer_cast<commons::hud>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_SOUND_PLAYER:
        return std::static_pointer_cast<commons::sound_player>(component)->serialize(destination, allocator);
    default:
        return false;
    }
}

bool parse_component(
    const std::string& name,
    const rapidjson::Value& data,
    commons::ecs& registry,
    commons::entity& entity,
    const std::filesystem::path& project_directory,
    const project_persistence_system::asset_loading_hooks* hooks = nullptr
) {
    if (name == "transform") {
        return registry.get<commons::transform>(entity.id)->analyze(data);
    }

    if (name == "renderer") {
        if (data.HasMember("model") && data["model"].IsString() &&
            std::string(data["model"].GetString()) != "") {
            std::filesystem::path model_path(data["model"].GetString());
            if (!model_path.is_absolute())
                model_path = project_directory / model_path;
            model_path = model_path.lexically_normal();
            const auto model_path_string = model_path.string();
            try {
                if (hooks && hooks->instantiate) {
                    auto prepared = hooks->instantiate(model_path_string);
                    if (!prepared) return false;
                    registry.add<commons::renderer>(entity);
                    registry.get<commons::renderer>(entity.id)->m_modelo = prepared.release();
                } else registry.add<commons::renderer>(entity, model_path_string.c_str());
            }
            catch (const std::exception&) { return false; }
        } else {
            registry.add<commons::renderer>(entity, new commons::model());
        }

        auto renderer = registry.get<commons::renderer>(entity.id);
        if (!renderer)
            return false;

        if (renderer->m_modelo && data.HasMember("materials") &&
            data.HasMember("model") && data["model"].GetStringLength() != 0) {
            const auto& materials = data["materials"];
            if (materials.Size() != renderer->m_modelo->meshes.size())
                return false;
            for (rapidjson::SizeType index = 0;
                 index < materials.Size();
                 ++index) {
                if (materials[index].HasMember("file")) {
                    const std::filesystem::path reference(materials[index]["file"].GetString());
                    commons::material loaded; std::string error;
                    const auto resolved = reference.is_absolute() ? reference : project_directory / reference;
                    try {
                        if (hooks && hooks->material) loaded = hooks->material(resolved);
                        else if (!commons::load_material_asset(resolved, loaded, error)) return false;
                    } catch (const std::exception&) { return false; }
                    renderer->m_modelo->meshes[index].setMaterial(loaded);
                    continue;
                }
                const auto& albedo = materials[index]["albedo"];
                auto material =
                    renderer->m_modelo->meshes[index].getMaterial();
                material.set_uniform(
                    "material.albedo",
                    commons::color{
                        albedo[0].GetFloat(),
                        albedo[1].GetFloat(),
                        albedo[2].GetFloat(),
                        albedo[3].GetFloat()
                    }
                );
                const auto& saved = materials[index];
                for (const auto& factor : commons::pbr_factors)
                    if (saved.HasMember(factor.name))
                        material.set_uniform(std::string("material.") + factor.name,
                            saved[factor.name].GetFloat());
                material.set_uniform("uvMundo", saved.HasMember("world_texture") && saved["world_texture"].GetBool());
                for (const auto& factor : commons::texture_factors)
                    material.set_uniform(std::string("material.") + factor.name,
                        saved.HasMember(factor.name) ? saved[factor.name].GetFloat() : factor.fallback);
                if (saved.HasMember("maps")) {
                    for (const auto& map : commons::pbr_maps) {
                        if (!saved["maps"].HasMember(map.name))
                            continue;
                        std::filesystem::path texture_path(saved["maps"][map.name].GetString());
                        const bool enabled = !texture_path.empty();
                        if (enabled) {
                            if (!texture_path.is_absolute())
                                texture_path = project_directory / texture_path;
                            texture_path = texture_path.lexically_normal();
                            const auto imported_texture = material.getTextura(map.name);
                            if (!imported_texture.embedded ||
                                imported_texture.path != texture_path.string()) {
                                material.set_texture(map.name,
                                    commons::texture{0, texture_path.string()});
                            }
                        } else {
                            material.textures.erase(map.name);
                        }
                        material.set_uniform(std::string("use_") + map.name, enabled);
                    }
                }
                renderer->m_modelo->meshes[index].setMaterial(material);
            }
        }
        if (data.HasMember("cast_shadows")) renderer->cast_shadows = data["cast_shadows"].GetBool();
        if (data.HasMember("receive_shadows")) renderer->receive_shadows = data["receive_shadows"].GetBool();
        if (data.HasMember("visible"))
            renderer->visible = data["visible"].GetBool();
        return true;
    }

    if (name == "code") {
        if (!registry.get<commons::code>(entity.id))
            registry.add<commons::code>(entity, "");
        auto script = registry.get<commons::code>(entity.id);
        if (!script->analyze(data))
            return false;
        script->asset_root = project_directory.string();
        const std::filesystem::path script_path(script->file);
        script->arquivoCompleto = (
            script_path.is_absolute()
                ? script_path
                : project_directory / script_path
        ).lexically_normal().string();
        for (auto& entry : script->scripts) {
            entry->asset_root = project_directory.string();
            const std::filesystem::path path(entry->file);
            entry->arquivoCompleto = (path.is_absolute() ? path : project_directory / path).lexically_normal().string();
        }
        // Discover defaults without running user code. Missing/invalid scripts remain
        // loadable; the Inspector and runtime report their errors when used.
        std::string property_error;
        script->load_properties(property_error);
        for (auto& entry : script->scripts) entry->load_properties(property_error);
        return true;
    }

    if (name == "physics") {
        const std::string shape_name(data["shape"].GetString());
        auto shape = commons::physics::shapes::box_shape;
        if (shape_name == "capsule")
            shape = commons::physics::shapes::forma_capsula;
        else if (shape_name == "sphere")
            shape = commons::physics::shapes::forma_esfera;
        else if (shape_name == "cylinder")
            shape = commons::physics::shapes::forma_cilindro;
        else if (shape_name == "cone")
            shape = commons::physics::shapes::forma_cone;
        else if (shape_name == "plane")
            shape = commons::physics::shapes::forma_plano;
        else if (shape_name == "mesh")
            shape = commons::physics::shapes::mesh_shape;
        const bool static_body =
            data.HasMember("static") && data["static"].GetBool();
        registry.add<commons::physics>(entity, static_body, shape);
        auto physics = registry.get<commons::physics>(entity.id);
        if (!physics->analyze(data))
            return false;
        if (static_body)
            physics->setMass(0.f);
        return true;
    }

    if (name == "camera") {
        registry.add<commons::camera>(entity);
        auto camera = registry.get<commons::camera>(entity.id);
        camera->skybox_asset_root = project_directory.string();
        return camera->analyze(data);
    }

    if (name == "directional_light") {
        registry.add<commons::directional_light>(entity);
        return registry.get<commons::directional_light>(entity.id)->analyze(data);
    }

    if (name == "spot_light") {
        registry.add<commons::spot_light>(entity);
        return registry.get<commons::spot_light>(entity.id)->analyze(data);
    }

    if (name == "sound_listener") {
        registry.add<commons::sound_listener>(entity);
        return registry.get<commons::sound_listener>(entity.id)->analyze(data);
    }

    if (name == "hud") {
        registry.add<commons::hud>(entity);
        return registry.get<commons::hud>(entity.id)->analyze(data);
    }
    if (name == "sound_player") {
        registry.add<commons::sound_player>(entity);
        return registry.get<commons::sound_player>(entity.id)->analyze(data);
    }

    return false;
}

bool validate_entities(const rapidjson::Value& entities, std::string& error) {
    if (!entities.IsArray()) {
        error = "The scene does not contain a valid entity list.";
        return false;
    }
    std::unordered_map<std::uint32_t, bool> entity_ids;
    std::unordered_map<std::uint32_t, std::uint32_t> parents;
    for (const auto& entity : entities.GetArray()) {
        if (!entity.IsObject() ||
            !entity.HasMember("id") ||
            !entity["id"].IsUint() ||
            entity["id"].GetUint() == 0 ||
            (entity.HasMember("name") && !entity["name"].IsString()) ||
            (entity.HasMember("locked") && !entity["locked"].IsBool()) ||
            (entity.HasMember("parent") && !entity["parent"].IsUint()) ||
            !entity.HasMember("components") ||
            !entity["components"].IsObject() ||
            !entity["components"].HasMember("transform")) {
            error = "The project contains an invalid entity record.";
            return false;
        }

        const auto id = entity["id"].GetUint();
        if (!entity_ids.emplace(id, true).second) {
            error = "The project contains duplicate entity IDs.";
            return false;
        }
        const auto parent = entity.HasMember("parent")
            ? entity["parent"].GetUint()
            : 0u;
        if (parent == id) {
            error = "An entity cannot be its own parent.";
            return false;
        }
        parents.emplace(id, parent);

        const auto& components = entity["components"];
        for (auto component = components.MemberBegin(); component != components.MemberEnd(); ++component) {
            const std::string name(component->name.GetString(), component->name.GetStringLength());
            if (!validate_component(name, component->value)) {
                error = "The project contains an unsupported or invalid component '" + name + "'.";
                return false;
            }
        }

    }

    for (const auto& [id, parent] : parents) {
        if (parent && !entity_ids.contains(parent)) {
            error = "Entity " + std::to_string(id) +
                " references a parent that does not exist.";
            return false;
        }
        std::unordered_set<std::uint32_t> visited;
        auto ancestor = id;
        while (ancestor) {
            if (!visited.insert(ancestor).second) {
                error = "The scene hierarchy contains a parent cycle.";
                return false;
            }
            const auto found = parents.find(ancestor);
            ancestor = found == parents.end() ? 0u : found->second;
        }
    }

    return true;
}

bool validate_project(const rapidjson::Document& document, std::string& error) {
    if (!document.IsObject() ||
        !document.HasMember("format") ||
        !document["format"].IsString() ||
        std::string(document["format"].GetString()) != "bubble-engine-project" ||
        !document.HasMember("version") ||
        !document["version"].IsUint() ||
        document["version"].GetUint() != project_migration_manager::current_format ||
        (!document.HasMember("scene") && !document.HasMember("entities"))) {
        error = "The file is not a supported Bubble Engine project.";
        return false;
    }

    if (!project_migration_manager::compatible(document, error)) return false;

    if (document.HasMember("settings") &&
        (!document["settings"].IsObject() ||
         (document["settings"].HasMember("gravity") &&
          !is_vector3(document["settings"]["gravity"])) ||
         (document["settings"].HasMember("editor_camera") &&
          !validate_component("camera", document["settings"]["editor_camera"])) ||
         (document["settings"].HasMember("editor_camera_transform") &&
          !validate_component("transform", document["settings"]["editor_camera_transform"])))) {
        error = "The project contains invalid global settings.";
        return false;
    }

    if (document.HasMember("scene") && !document["scene"].IsString()) {
        error = "The project contains an invalid main-scene path.";
        return false;
    }
    if (document.HasMember("entities") &&
        !validate_entities(document["entities"], error))
        return false;
    return true;
}

bool validate_scene(const rapidjson::Document& document, std::string& error) {
    if (!document.IsObject() ||
        !document.HasMember("format") ||
        !document["format"].IsString() ||
        std::string(document["format"].GetString()) != "bubble-engine-scene" ||
        !document.HasMember("version") ||
        !document["version"].IsUint() ||
        document["version"].GetUint() != 1 ||
        !document.HasMember("entities")) {
        error = "The file is not a supported Bubble Engine scene.";
        return false;
    }
    return validate_entities(document["entities"], error);
}


}

project_persistence_system::project_persistence_system(
    std::shared_ptr<commons::ecs> registry,
    const std::uint32_t editor_camera_id,
    entity_name_map* entity_names,
    entity_state_set* hidden_entities,
    entity_state_set* locked_entities
)
    : m_registry(std::move(registry)),
      m_editor_camera_id(editor_camera_id),
      m_entity_names(entity_names),
      m_hidden_entities(hidden_entities),
      m_locked_entities(locked_entities) {}

void project_persistence_system::set_entity_names(entity_name_map* entity_names) {
    m_entity_names = entity_names;
}

const std::array<float, 3>& project_persistence_system::global_gravity() const noexcept {
    return m_global_gravity;
}

void project_persistence_system::set_global_gravity(
    const std::array<float, 3>& gravity
) noexcept {
    m_global_gravity = gravity;
}


bool project_persistence_system::save(
    const std::string& path,
    std::string& error
) const {
    if (bgui::resource_files::embedded()) { error = "Embedded game resources are read-only."; return false; }
    std::string snapshot;
    if (!serialize_project(path, error, true, snapshot))
        return false;
    m_saved_states[saved_state_key(path)] = {std::move(snapshot), m_scene_path};
    return true;
}

bool project_persistence_system::has_unsaved_changes(const std::string& path) const {
    const auto saved = m_saved_states.find(saved_state_key(path));
    if (saved == m_saved_states.end())
        return true;
    std::string snapshot, error;
    if (!serialize_project(path, error, false, snapshot))
        return true;
    rapidjson::Document current, previous;
    current.Parse(snapshot.c_str());
    previous.Parse(saved->second.snapshot.c_str());
    return current.HasParseError() || previous.HasParseError() || current != previous;
}

bool project_persistence_system::serialize_project(
    const std::string& path, std::string& error,
    bool write_files, std::string& snapshot
) const {
    error.clear();
    snapshot = "[";
    std::vector<project_files::document> pending_documents;
    const auto emit = [&](const std::filesystem::path& target,
                          const rapidjson::Document& document) {
        rapidjson::Document canonical;
        canonical.CopyFrom(document, canonical.GetAllocator());
        if (canonical.HasMember("entities")) {
            auto& entities = canonical["entities"];
            std::sort(entities.Begin(), entities.End(), [](const auto& a, const auto& b) {
                return a["id"].GetUint() < b["id"].GetUint();
            });
        }
        auto encoded = project_files::encode(canonical);
        if (snapshot.size() > 1)
            snapshot += ',';
        snapshot += encoded;
        if (write_files)
            pending_documents.push_back({target, std::move(encoded)});
    };
    if (!m_registry) {
        error = "Cannot save a project without an ECS registry.";
        return false;
    }
    if (!m_registry->get<commons::camera>(m_editor_camera_id) ||
        !m_registry->get<commons::transform>(m_editor_camera_id)) {
        error = "The editor camera entity is not available.";
        return false;
    }

    std::error_code path_error;
    const auto absolute_target = std::filesystem::absolute(
        std::filesystem::path(path),
        path_error
    ).lexically_normal();
    if (path_error) {
        error = "Could not resolve project path: " + path_error.message();
        return false;
    }

    const bool writing_scene = is_scene_path(absolute_target);
    auto scene_target = absolute_target;
    if (!writing_scene) {
        const auto saved = m_saved_states.find(absolute_target.string());
        if (saved != m_saved_states.end()) {
            scene_target = saved->second.scene_path;
        } else if (m_project_path == absolute_target && !m_scene_path.empty()) {
            scene_target = m_scene_path;
        } else if (!m_asset_root.empty() &&
                   absolute_target.parent_path() == m_asset_root) {
            scene_target = m_asset_root / "main.scene";
        } else {
            scene_target = absolute_target.parent_path() /
                (absolute_target.stem().string() + ".scene");
        }
        if (!bgui::resource_files::embedded() && !m_asset_root.empty() &&
            absolute_target.parent_path() == m_asset_root) {
            std::ifstream marker(m_asset_root / ".bengine-main-scene");
            std::string marker_path;
            if (marker && std::getline(marker, marker_path) &&
                !marker_path.empty()) {
                scene_target = (m_asset_root / marker_path).lexically_normal();
            }
        }
    }

    rapidjson::Document scene_document;
    scene_document.SetObject();
    auto& allocator = scene_document.GetAllocator();
    rapidjson::Value scene_format;
    scene_format.SetString("bubble-engine-scene", allocator);
    scene_document.AddMember("format", scene_format, allocator);
    scene_document.AddMember("version", 1, allocator);
    rapidjson::Value entities(rapidjson::kArrayType);

    for (const auto& [id, components] : m_registry->entities) {
        if (id == m_editor_camera_id)
            continue;

        rapidjson::Value entity(rapidjson::kObjectType);
        entity.AddMember("id", id, allocator);
        entity.AddMember(
            "locked",
            m_locked_entities && m_locked_entities->contains(id),
            allocator
        );
        const auto transform = m_registry->get<commons::transform>(id);
        if (transform && transform->parent_id)
            entity.AddMember("parent", transform->parent_id, allocator);

        {
            const auto& names = m_entity_names ? *m_entity_names : m_registry->entity_names;
            const auto name_it = names.find(id);
            if (name_it != names.end() && !name_it->second.empty()) {
                rapidjson::Value name_value;
                name_value.SetString(name_it->second.c_str(), static_cast<rapidjson::SizeType>(name_it->second.size()), allocator);
                entity.AddMember("name", name_value, allocator);
            }
        }

        rapidjson::Value serialized_components(rapidjson::kObjectType);
        for (const auto& [mask, component] : components) {
            const auto name = component_name(mask);
            if (name.empty()) {
                error = "Cannot save entity " + std::to_string(id) +
                    ": component mask " + std::to_string(static_cast<std::uint32_t>(mask)) +
                    " does not support project serialization.";
                return false;
            }

            rapidjson::Value component_data(rapidjson::kObjectType);
            if (!serialize_component(component, mask, component_data, allocator)) {
                error = "Could not serialize component '" + name +
                    "' on entity " + std::to_string(id) + ".";
                return false;
            }

            const bool saving_in_project =
                absolute_target.parent_path() == m_asset_root;
            if (mask == commons::component::COMPONENTE_CODIGO &&
                !m_asset_root.empty() && !saving_in_project) {
                const auto script =
                    std::static_pointer_cast<commons::code>(component);
                const std::filesystem::path script_path(script->file);
                const auto resolved_script_path = (
                    script_path.is_absolute()
                        ? script_path
                        : m_asset_root / script_path
                ).lexically_normal().string();
                component_data["directory"].SetString(
                    resolved_script_path.c_str(),
                    static_cast<rapidjson::SizeType>(resolved_script_path.size()),
                    allocator
                );
                const auto resolve_images = [&](rapidjson::Value& properties) {
                    for (auto& property : properties.GetArray()) {
                        if (std::string_view(property["type"].GetString()) != "image") continue;
                        auto& value = property["value"];
                        if (!value.GetStringLength()) continue;
                        const auto path = (m_asset_root / value.GetString()).lexically_normal().string();
                        value.SetString(path.c_str(), static_cast<rapidjson::SizeType>(path.size()), allocator);
                    }
                };
                if (component_data.HasMember("properties")) resolve_images(component_data["properties"]);
                if (component_data.HasMember("script_properties"))
                    for (auto& properties : component_data["script_properties"].GetArray()) resolve_images(properties);
                if (component_data.HasMember("scripts")) {
                    for (auto& entry : component_data["scripts"].GetArray()) {
                        const std::filesystem::path path(entry.GetString());
                        const auto resolved = (path.is_absolute() ? path : m_asset_root / path).lexically_normal().string();
                        entry.SetString(resolved.c_str(), static_cast<rapidjson::SizeType>(resolved.size()), allocator);
                    }
                }
            } else if (mask == commons::component::COMPONENTE_CAM &&
                       !m_asset_root.empty() && !saving_in_project) {
                const auto resolve_image = [&](rapidjson::Value& value) {
                    if (!value.GetStringLength()) return;
                    const std::filesystem::path path(value.GetString());
                    const auto absolute = (path.is_absolute() ? path : m_asset_root / path).lexically_normal().string();
                    value.SetString(absolute.c_str(), static_cast<rapidjson::SizeType>(absolute.size()), allocator);
                };
                resolve_image(component_data["skybox_image"]);
                for (auto& face : component_data["skybox_faces"].GetArray()) resolve_image(face);
            } else if (mask == commons::component::COMPONENTE_RENDER &&
                       !m_asset_root.empty()) {
                const auto renderer =
                    std::static_pointer_cast<commons::renderer>(component);
                if (!renderer->m_modelo) {
                    error = "Could not serialize the model on entity " +
                        std::to_string(id) + ".";
                    return false;
                }
                const std::filesystem::path model_path(
                    renderer->m_modelo->get_source_path()
                );
                if (!model_path.empty()) {
                    std::filesystem::path saved_model_path = model_path;
                    if (saving_in_project) {
                        const auto relative_model_path =
                            model_path.lexically_relative(m_asset_root);
                        if (!relative_model_path.empty() &&
                            *relative_model_path.begin() != "..") {
                            saved_model_path = relative_model_path;
                        }
                    }
                    const auto saved_model = saved_model_path.generic_string();
                    component_data["model"].SetString(
                        saved_model.c_str(),
                        static_cast<rapidjson::SizeType>(saved_model.size()),
                        allocator
                    );
                }
            }

            if (mask == commons::component::COMPONENTE_RENDER) {
                const auto renderer =
                    std::static_pointer_cast<commons::renderer>(component);
                if (!renderer->m_modelo) {
                    error = "Could not serialize the model on entity " +
                        std::to_string(id) + ".";
                    return false;
                }
                rapidjson::Value materials(rapidjson::kArrayType);
                for (const auto& mesh : renderer->m_modelo->meshes) {
                    const auto source_material = mesh.getMaterial();
                    const auto albedo = material_albedo(source_material);
                    rapidjson::Value material(rapidjson::kObjectType);
                    rapidjson::Value color(rapidjson::kArrayType);
                    color.PushBack(albedo.r, allocator);
                    color.PushBack(albedo.g, allocator);
                    color.PushBack(albedo.b, allocator);
                    color.PushBack(albedo.a, allocator);
                    material.AddMember("albedo", color, allocator);
                    for (const auto& factor : commons::pbr_factors) {
                        rapidjson::Value key(factor.name, allocator);
                        material.AddMember(key, commons::pbr_value(source_material, factor), allocator);
                    }
                    material.AddMember("world_texture", commons::world_texture(source_material), allocator);
                    for (const auto& factor : commons::texture_factors) {
                        rapidjson::Value key(factor.name, allocator);
                        material.AddMember(key, commons::pbr_value(source_material, factor), allocator);
                    }
                    rapidjson::Value maps(rapidjson::kObjectType);
                    for (const auto& map : commons::pbr_maps) {
                        const auto texture = source_material.getTextura(map.name);
                        std::filesystem::path texture_path(texture.embedded ? std::string{} : texture.path);
                        if (!texture_path.empty() && saving_in_project) {
                            const auto relative = texture_path.lexically_relative(m_asset_root);
                            if (!relative.empty() && *relative.begin() != "..")
                                texture_path = relative;
                        }
                        rapidjson::Value key(map.name, allocator);
                        rapidjson::Value value(texture_path.generic_string().c_str(), allocator);
                        maps.AddMember(key, value, allocator);
                    }
                    material.AddMember("maps", maps, allocator);
                    try {
                        const auto directory=absolute_target.parent_path()/(absolute_target.stem().string()+".materials");
                        std::filesystem::path file;
                        if (write_files) {
                            if (m_material_save_hook) file = m_material_save_hook(source_material, directory);
                            else if (!commons::store_material_asset(directory,source_material,file,error)) return false;
                        } else {
                            const auto contents=commons::encode_material_asset(source_material,directory);
                            file=commons::material_asset_path(directory,contents);
                        }
                        const auto reference=saving_in_project ? file.lexically_relative(m_asset_root) : file;
                        material.AddMember("file",rapidjson::Value(reference.generic_string().c_str(),allocator),allocator);
                    } catch (const std::exception& e) { error=e.what();return false; }
                    materials.PushBack(material, allocator);
                }
                component_data.AddMember("materials", materials, allocator);
                component_data.AddMember("visible", renderer->visible, allocator);
            }

            rapidjson::Value component_key;
            component_key.SetString(name.c_str(), static_cast<rapidjson::SizeType>(name.size()), allocator);
            serialized_components.AddMember(component_key, component_data, allocator);
        }
        entity.AddMember("components", serialized_components, allocator);
        entities.PushBack(entity, allocator);
    }

    scene_document.AddMember("entities", entities, allocator);
    emit(scene_target, scene_document);

    if (writing_scene) {
        snapshot += ']';
        if (write_files) {
            if (!project_files::save(absolute_target, pending_documents, error))
                return false;
            m_scene_path = absolute_target;
        }
        return true;
    }

    rapidjson::Document project_document;
    project_document.SetObject();
    auto& project_allocator = project_document.GetAllocator();
    rapidjson::Value project_format;
    project_format.SetString("bubble-engine-project", project_allocator);
    project_document.AddMember("format", project_format, project_allocator);
    project_document.AddMember("version", project_migration_manager::current_format, project_allocator);
    project_migration_manager::add_manifest(project_document, m_project_version);

    rapidjson::Value settings(rapidjson::kObjectType);
    rapidjson::Value gravity(rapidjson::kArrayType);
    for (const auto axis : m_global_gravity)
        gravity.PushBack(axis, project_allocator);
    settings.AddMember("gravity", gravity, project_allocator);
    rapidjson::Value editor_camera_transform(rapidjson::kObjectType);
    m_registry->get<commons::transform>(m_editor_camera_id)->serialize(
        editor_camera_transform, project_allocator
    );
    settings.AddMember("editor_camera_transform", editor_camera_transform, project_allocator);
    rapidjson::Value editor_camera(rapidjson::kObjectType);
    if (!m_registry->get<commons::camera>(m_editor_camera_id)->serialize(editor_camera, project_allocator)) {
        error = "The editor camera settings are invalid.";
        return false;
    }
    settings.AddMember("editor_camera", editor_camera, project_allocator);
    project_document.AddMember("settings", settings, project_allocator);

    auto scene_reference = scene_target.lexically_relative(
        absolute_target.parent_path()
    );
    const auto scene_reference_string = scene_reference.generic_string();
    rapidjson::Value scene_path_value;
    scene_path_value.SetString(
        scene_reference_string.c_str(),
        static_cast<rapidjson::SizeType>(scene_reference_string.size()),
        project_allocator
    );
    project_document.AddMember("scene", scene_path_value, project_allocator);

    emit(absolute_target, project_document);

    snapshot += ']';
    if (write_files) {
        if (!project_files::save(absolute_target, pending_documents, error))
            return false;
        m_project_path = absolute_target;
        m_scene_path = scene_target;
    }
    return true;
}

bool project_persistence_system::load(
    const std::string& path,
    std::string& error
) const {
    error.clear();
    if (!m_registry) {
        error = "Cannot load a project without an ECS registry.";
        return false;
    }

    if (!bgui::resource_files::embedded() && !project_files::recover(path, error))
        return false;
    if (!bgui::resource_files::exists(path)) { error = "Could not open project file: " + path; return false; }
    auto input = bgui::resource_files::stream(path);
    if (!input) {
        error = "Could not open project file '" + path + "'.";
        return false;
    }

    const std::string contents{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    if (input.bad()) {
        error = "Could not read project file '" + path + "'.";
        return false;
    }
    rapidjson::Document document;
    document.Parse(contents.data(), contents.size());
    if (document.HasParseError()) {
        error = "Project file has invalid JSON at byte " +
            std::to_string(document.GetErrorOffset()) + ".";
        return false;
    }

    std::error_code path_error;
    const auto absolute_project_path = std::filesystem::absolute(
        std::filesystem::path(path),
        path_error
    ).lexically_normal();
    if (path_error) {
        error = "Could not resolve project path: " + path_error.message();
        return false;
    }
    const auto project_directory = absolute_project_path.parent_path();

    rapidjson::Document scene_document;
    const rapidjson::Value* scene_data = &document;
    auto staged_scene_path = absolute_project_path;
    const bool is_project_document =
        document.IsObject() &&
        document.HasMember("format") &&
        document["format"].IsString() &&
        std::string(document["format"].GetString()) == "bubble-engine-project";
    bool migration_required = false;
    if (is_project_document) {
        if (!project_migration_manager::convert(document, migration_required, error)) return false;
        if (!validate_project(document, error))
            return false;

        if (document.HasMember("scene")) {
            const std::filesystem::path referenced_scene(
                document["scene"].GetString()
            );
            staged_scene_path = (
                referenced_scene.is_absolute()
                    ? referenced_scene
                    : project_directory / referenced_scene
            ).lexically_normal();
            if (!bgui::resource_files::exists(staged_scene_path)) { error = "Could not open scene file: " + staged_scene_path.string(); return false; }
            auto scene_input = bgui::resource_files::stream(staged_scene_path);
            if (!scene_input) {
                error = "Could not open scene file '" +
                    staged_scene_path.string() + "'.";
                return false;
            }
            const std::string scene_contents{
                std::istreambuf_iterator<char>(scene_input),
                std::istreambuf_iterator<char>()
            };
            if (scene_input.bad()) {
                error = "Could not read scene file '" +
                    staged_scene_path.string() + "'.";
                return false;
            }
            scene_document.Parse(scene_contents.data(), scene_contents.size());
            if (scene_document.HasParseError()) {
                error = "Scene file has invalid JSON at byte " +
                    std::to_string(scene_document.GetErrorOffset()) + ".";
                return false;
            }
            if (!validate_scene(scene_document, error))
                return false;
            scene_data = &scene_document;
        } else {
            staged_scene_path = project_directory /
                (absolute_project_path.stem().string() + ".scene");
        }
    } else if (!validate_scene(document, error)) {
        return false;
    }

    std::array<float, 3> staged_gravity{0.f, -9.8f, 0.f};
    std::optional<rapidjson::Value> staged_editor_camera_transform;
    if (document.HasMember("settings") &&
        document["settings"].HasMember("gravity")) {
        const auto& gravity = document["settings"]["gravity"];
        for (rapidjson::SizeType axis = 0; axis < gravity.Size(); ++axis)
            staged_gravity[axis] = gravity[axis].GetFloat();
    }
    if (is_project_document && document.HasMember("settings") &&
        document["settings"].HasMember("editor_camera_transform")) {
        staged_editor_camera_transform.emplace(
            document["settings"]["editor_camera_transform"],
            document.GetAllocator()
        );
    }

    auto active_camera = m_registry->get<commons::camera>(m_editor_camera_id);
    auto active_transform = m_registry->get<commons::transform>(m_editor_camera_id);
    if (!active_camera || !active_transform) {
        error = "The editor camera entity is not available.";
        return false;
    }

    for (const auto& entity : (*scene_data)["entities"].GetArray()) {
        if (entity["id"].GetUint() != m_editor_camera_id)
            continue;

        if (!entity["components"].HasMember("camera")) {
            error = "The project contains an entity that conflicts with the editor camera ID.";
            return false;
        }
        break;
    }

    if (m_asset_hooks.prepare) {
        std::vector<std::string> paths;
        for (const auto& item : (*scene_data)["entities"].GetArray()) {
            if (item["id"].GetUint() == m_editor_camera_id) continue;
            const auto& components = item["components"];
            if (!components.HasMember("renderer")) continue;
            const auto& renderer = components["renderer"];
            if (!renderer.HasMember("model") || !renderer["model"].IsString() || !renderer["model"].GetStringLength()) continue;
            paths.push_back((project_directory / renderer["model"].GetString()).lexically_normal().string());
        }
        try { m_asset_hooks.prepare(paths); }
        catch (const std::exception& failure) { error = failure.what(); return false; }
    }

    auto staged_registry = std::make_shared<commons::ecs>();
    entity_name_map staged_entity_names;
    entity_state_set staged_hidden_entities;
    entity_state_set staged_locked_entities;
    std::unordered_map<std::uint32_t, commons::entity> staged_entities;
    for (const auto& entity_data : (*scene_data)["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        if (id == m_editor_camera_id)
            continue;

        if (entity_data.HasMember("parent") &&
            entity_data["parent"].GetUint() == m_editor_camera_id) {
            error = "Scene entities cannot use the editor camera as a parent.";
            return false;
        }

        if (entity_data.HasMember("name") && entity_data["name"].IsString())
            staged_entity_names.emplace(id, entity_data["name"].GetString());
        if (entity_data.HasMember("locked") &&
            entity_data["locked"].GetBool()) {
            staged_locked_entities.insert(id);
        }
        staged_entities.emplace(id, staged_registry->create(id));
    }

    staged_registry->entity_names = staged_entity_names;

    // Build every local transform and parent link before loading scripts. Script
    // setup callbacks can then safely read their own and their parent's pose.
    for (const auto& entity_data : (*scene_data)["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        if (id == m_editor_camera_id)
            continue;
        const auto& components = entity_data["components"];
        const auto transform_member = components.FindMember("transform");
        auto& entity = staged_entities.at(id);
        if (transform_member == components.MemberEnd() ||
            !parse_component(
                "transform",
                transform_member->value,
                *staged_registry,
                entity,
                project_directory
            )) {
            error = "Could not load transform on entity " + std::to_string(id) + ".";
            return false;
        }
        if (entity_data.HasMember("parent"))
            staged_registry->get<commons::transform>(id)->parent_id =
                entity_data["parent"].GetUint();
    }

    for (const auto& entity_data : (*scene_data)["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        if (id == m_editor_camera_id)
            continue;
        const auto& components = entity_data["components"];
        auto& entity = staged_entities.at(id);
        for (auto component = components.MemberBegin(); component != components.MemberEnd(); ++component) {
            const std::string name(component->name.GetString(), component->name.GetStringLength());
            if (name == "transform" || name == "code")
                continue;
            if (!parse_component(
                    name,
                    component->value,
                    *staged_registry,
                    entity,
                    project_directory,
                    &m_asset_hooks
                )) {
                error = "Could not load component '" + name +
                    "' on entity " + std::to_string(id) + ".";
                return false;
            }
        }
    }

    // Legacy spotlights applied AO per cone. Use the first enabled setting by entity ID
    // for cameras that do not already declare their own post-processing.
    std::optional<commons::camera_post_processing> legacy_occlusion;
    std::uint32_t legacy_light_id = 0;
    for (const auto& item : (*scene_data)["entities"].GetArray()) {
        const auto id = item["id"].GetUint();
        const auto& components = item["components"];
        if (id == m_editor_camera_id || !components.HasMember("spot_light") ||
            (legacy_occlusion && id >= legacy_light_id))
            continue;
        const auto& light = components["spot_light"];
        if (!light.HasMember("ssao_enabled") && !light.HasMember("ssao_radius") &&
            !light.HasMember("ssao_strength"))
            continue;
        if (light.HasMember("ssao_enabled") &&
            (!light["ssao_enabled"].IsBool() || !light["ssao_enabled"].GetBool()))
            continue;
        commons::camera_post_processing settings;
        settings.occlusion = commons::ambient_occlusion_mode::ssao;
        if (light.HasMember("ssao_radius"))
            settings.radius = is_number(light["ssao_radius"]) ? light["ssao_radius"].GetFloat() : 0.f;
        if (light.HasMember("ssao_strength"))
            settings.strength = is_number(light["ssao_strength"]) ? light["ssao_strength"].GetFloat() : -1.f;
        if (!settings.valid() || settings.strength == 0.f ||
            staged_registry->get<commons::spot_light>(id)->intensity == 0.f)
            continue;
        legacy_occlusion = settings;
        legacy_light_id = id;
    }
    if (legacy_occlusion) {
        for (const auto& item : (*scene_data)["entities"].GetArray()) {
            const auto id = item["id"].GetUint();
            const auto& components = item["components"];
            if (id != m_editor_camera_id && components.HasMember("camera") &&
                !components["camera"].HasMember("post_processing"))
                staged_registry->get<commons::camera>(id)->config.post_processing = *legacy_occlusion;
        }
    }

    if (m_asset_hooks.finish) {
        try { m_asset_hooks.finish(staged_registry); }
        catch (const std::exception& failure) { error = failure.what(); return false; }
    }

    for (const auto& entity_data : (*scene_data)["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        if (id == m_editor_camera_id)
            continue;
        const auto& components = entity_data["components"];
        const auto code_member = components.FindMember("code");
        if (code_member != components.MemberEnd()) {
            auto& entity = staged_entities.at(id);
            if (!parse_component(
                    "code",
                    code_member->value,
                    *staged_registry,
                    entity,
                    project_directory
                )) {
                error = "Could not load code on entity " + std::to_string(id) + ".";
                return false;
            }
        }
        const auto renderer = staged_registry->get<commons::renderer>(id);
        if (renderer && !renderer->visible)
            staged_hidden_entities.insert(id);
    }

    auto editor_camera_entity = staged_registry->create(m_editor_camera_id);
    staged_registry->add<commons::camera>(editor_camera_entity);
    auto staged_scene_entity_names = staged_entity_names;
    const auto staged_asset_root = project_directory.lexically_normal();
    const auto staged_global_gravity = staged_gravity;
    const auto staged_project_path = is_project_document
        ? absolute_project_path
        : std::filesystem::path{};

    if (migration_required && m_migration_writes_enabled && !bgui::resource_files::embedded() &&
        !project_migration_manager::commit(absolute_project_path, document, error, &contents)) return false;
    m_project_version = is_project_document ? document["project_version"].GetString() : "0.1.0";

    auto& editor_components = staged_registry->entities[m_editor_camera_id];
    editor_components[commons::transform::mask] = active_transform;
    editor_components[commons::camera::mask] = active_camera;

    m_registry->entities = std::move(staged_registry->entities);
    m_registry->mascaras = std::move(staged_registry->mascaras);
    m_registry->live_entities = std::move(staged_registry->live_entities);
    m_registry->entity_names = std::move(staged_registry->entity_names);
    if (is_project_document && document.HasMember("settings") &&
        document["settings"].HasMember("editor_camera")) {
        const auto& editor_camera = document["settings"]["editor_camera"];
        active_camera->analyze(editor_camera);
        if (legacy_occlusion && !editor_camera.HasMember("post_processing"))
            active_camera->config.post_processing = *legacy_occlusion;
    }
    active_camera->skybox_asset_root = project_directory.string();
    if (staged_editor_camera_transform)
        active_transform->analyze(*staged_editor_camera_transform);
    active_transform->parent_id = 0;
    for (auto& [id, components] : m_registry->entities) {
        for (auto& [mask, component] : components) {
            (void)mask;
            component->reg = m_registry;
            component->my_object = id;
        }
    }

    if (m_entity_names)
        *m_entity_names = std::move(staged_scene_entity_names);
    if (m_hidden_entities)
        *m_hidden_entities = std::move(staged_hidden_entities);
    if (m_locked_entities)
        *m_locked_entities = std::move(staged_locked_entities);
    m_asset_root = staged_asset_root;
    m_project_path = staged_project_path;
    m_scene_path = staged_scene_path;
    m_global_gravity = staged_global_gravity;

    std::string snapshot, snapshot_error;
    m_saved_states.erase(saved_state_key(path));
    if (serialize_project(path, snapshot_error, false, snapshot))
        m_saved_states[saved_state_key(path)] = {std::move(snapshot), m_scene_path};

    return true;
}

}
