#include "project_persistence_system.hpp"

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "entities/entity.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

namespace editor::ui {
namespace {

using component_mask = commons::component::mask;

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

bool validate_component(const std::string& name, const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;

    if (name == "transform") {
        return value.HasMember("position") && is_vector3(value["position"]) &&
               value.HasMember("rotation") && is_vector3(value["rotation"]) &&
               value.HasMember("scale") && is_vector3(value["scale"]);
    }

    if (name == "renderer")
        return value.HasMember("model") && value["model"].IsString();

    if (name == "camera") {
        if ((value.HasMember("fov") && !is_number(value["fov"])) ||
            (value.HasMember("zfar") && !is_number(value["zfar"])) ||
            (value.HasMember("scale") && !is_number(value["scale"])) ||
            (value.HasMember("ortho") && !value["ortho"].IsBool()) ||
            (value.HasMember("skybox") && !value["skybox"].IsBool()))
            return false;

        if (value.HasMember("ceu")) {
            const auto& color = value["ceu"];
            if (!color.IsArray() || color.Size() != 4)
                return false;
            for (const auto& channel : color.GetArray())
                if (!is_number(channel))
                    return false;
        }
        return true;
    }

    if (name == "directional_light") {
        return value.HasMember("direction") && is_vector3(value["direction"]) &&
               value.HasMember("ambient") && is_vector3(value["ambient"]) &&
               value.HasMember("color") && is_vector3(value["color"]) &&
               value.HasMember("intensity") && is_number(value["intensity"]);
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
    case commons::component::COMPONENTE_LUZ_DIRECIONAL:
        return "directional_light";
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
    case commons::component::COMPONENTE_LUZ_DIRECIONAL:
        return std::static_pointer_cast<commons::directional_light>(component)->serialize(destination, allocator);
    default:
        return false;
    }
}

bool parse_component(
    const std::string& name,
    const rapidjson::Value& data,
    commons::ecs& registry,
    commons::entity& entity
) {
    if (name == "transform") {
        return registry.get<commons::transform>(entity.id)->analyze(data);
    }

    if (name == "renderer") {
        registry.add<commons::renderer>(entity);
        return registry.get<commons::renderer>(entity.id)->analyze(data);
    }

    if (name == "camera") {
        registry.add<commons::camera>(entity);
        return registry.get<commons::camera>(entity.id)->analyze(data);
    }

    if (name == "directional_light") {
        registry.add<commons::directional_light>(entity);
        return registry.get<commons::directional_light>(entity.id)->analyze(data);
    }

    return false;
}

bool validate_project(const rapidjson::Document& document, std::string& error) {
    if (!document.IsObject() ||
        !document.HasMember("format") ||
        !document["format"].IsString() ||
        std::string(document["format"].GetString()) != "bubble-engine-project" ||
        !document.HasMember("version") ||
        !document["version"].IsUint() ||
        document["version"].GetUint() != 1 ||
        !document.HasMember("entities") ||
        !document["entities"].IsArray()) {
        error = "The file is not a supported Bubble Engine project.";
        return false;
    }

    std::unordered_map<std::uint32_t, bool> entity_ids;
    for (const auto& entity : document["entities"].GetArray()) {
        if (!entity.IsObject() ||
            !entity.HasMember("id") ||
            !entity["id"].IsUint() ||
            entity["id"].GetUint() == 0 ||
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

        const auto& components = entity["components"];
        for (auto component = components.MemberBegin(); component != components.MemberEnd(); ++component) {
            const std::string name(component->name.GetString(), component->name.GetStringLength());
            if (!validate_component(name, component->value)) {
                error = "The project contains an unsupported or invalid component '" + name + "'.";
                return false;
            }
        }

    }

    return true;
}

}

project_persistence_system::project_persistence_system(
    std::shared_ptr<commons::ecs> registry,
    const std::uint32_t editor_camera_id
)
    : m_registry(std::move(registry)),
      m_editor_camera_id(editor_camera_id) {}

bool project_persistence_system::save(
    const std::string& path,
    std::string& error
) const {
    error.clear();
    if (!m_registry) {
        error = "Cannot save a project without an ECS registry.";
        return false;
    }

    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();

    rapidjson::Value format;
    format.SetString("bubble-engine-project", allocator);
    document.AddMember("format", format, allocator);
    document.AddMember("version", 1, allocator);
    rapidjson::Value entities(rapidjson::kArrayType);

    for (const auto& [id, components] : m_registry->entities) {
        rapidjson::Value entity(rapidjson::kObjectType);
        entity.AddMember("id", id, allocator);
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

            rapidjson::Value component_key;
            component_key.SetString(name.c_str(), static_cast<rapidjson::SizeType>(name.size()), allocator);
            serialized_components.AddMember(component_key, component_data, allocator);
        }
        entity.AddMember("components", serialized_components, allocator);
        entities.PushBack(entity, allocator);
    }

    document.AddMember("entities", entities, allocator);
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open project file '" + path + "' for writing.";
        return false;
    }
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    if (!output) {
        error = "Could not finish writing project file '" + path + "'.";
        return false;
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

    std::ifstream input(path, std::ios::binary);
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
    if (!validate_project(document, error))
        return false;

    bool has_editor_camera = false;
    for (const auto& entity : document["entities"].GetArray()) {
        if (entity["id"].GetUint() == m_editor_camera_id &&
            entity["components"].HasMember("camera")) {
            has_editor_camera = true;
            break;
        }
    }
    if (!has_editor_camera) {
        error = "The project does not contain the editor camera entity (ID " +
            std::to_string(m_editor_camera_id) + ").";
        return false;
    }

    auto staged_registry = std::make_shared<commons::ecs>();
    for (const auto& entity_data : document["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        auto entity = staged_registry->create(id);
        const auto& components = entity_data["components"];
        for (auto component = components.MemberBegin(); component != components.MemberEnd(); ++component) {
            const std::string name(component->name.GetString(), component->name.GetStringLength());
            if (!parse_component(name, component->value, *staged_registry, entity)) {
                error = "Could not load component '" + name +
                    "' on entity " + std::to_string(id) + ".";
                return false;
            }
        }
    }

    auto active_camera = m_registry->get<commons::camera>(m_editor_camera_id);
    auto staged_camera = staged_registry->get<commons::camera>(m_editor_camera_id);
    if (active_camera && staged_camera) {
        const auto& camera_data = document["entities"].GetArray();
        for (const auto& entity : camera_data) {
            if (entity["id"].GetUint() == m_editor_camera_id) {
                if (!active_camera->analyze(entity["components"]["camera"])) {
                    error = "Could not apply the project settings to the editor camera.";
                    return false;
                }
                break;
            }
        }
        active_camera->m_transform = nullptr;
        staged_registry->entities[m_editor_camera_id][commons::camera::mask] = active_camera;
    }

    m_registry->entities = std::move(staged_registry->entities);
    m_registry->mascaras = std::move(staged_registry->mascaras);
    for (auto& [id, components] : m_registry->entities) {
        for (auto& [mask, component] : components) {
            (void)mask;
            component->reg = m_registry;
            component->my_object = id;
        }
    }

    return true;
}

}
