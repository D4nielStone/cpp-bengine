#include "project_persistence_system.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/directional_light.hpp"
#include "components/physics.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "entities/entity.hpp"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <fstream>
#include <filesystem>
#include <iterator>
#include <memory>
#include <algorithm>
#include <cctype>
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
        if (value.HasMember("visible") && !value["visible"].IsBool())
            return false;
        if (!value.HasMember("model") && !value.HasMember("visible"))
            return false;
        if (!value.HasMember("materials"))
            return true;
        if (!value["materials"].IsArray())
            return false;
        for (const auto& material : value["materials"].GetArray()) {
            if (!material.IsObject() || !material.HasMember("albedo") ||
                !is_color4(material["albedo"]))
                return false;
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
            shape != "cylinder" && shape != "cone" && shape != "plane")
            return false;
        if ((value.HasMember("static") && !value["static"].IsBool()) ||
            (value.HasMember("mass") &&
                (!is_number(value["mass"]) || value["mass"].GetFloat() < 0.f)) ||
            (value.HasMember("friction") && !is_number(value["friction"])) ||
            (value.HasMember("restitution") &&
                !is_number(value["restitution"])))
            return false;
        return true;
    }

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
    case commons::component::COMPONENTE_CODIGO:
        return "code";
    case commons::component::COMPONENTE_FISICA:
        return "physics";
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
    case commons::component::COMPONENTE_CODIGO:
        return std::static_pointer_cast<commons::code>(component)->serialize(destination, allocator);
    case commons::component::COMPONENTE_FISICA:
        return std::static_pointer_cast<commons::physics>(component)->serialize(destination, allocator);
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
    commons::entity& entity,
    const std::filesystem::path& project_directory
) {
    if (name == "transform") {
        return registry.get<commons::transform>(entity.id)->analyze(data);
    }

    if (name == "renderer") {
        if (data.HasMember("model")) {
            std::filesystem::path model_path(data["model"].GetString());
            if (!model_path.is_absolute())
                model_path = project_directory / model_path;
            model_path = model_path.lexically_normal();
            const auto model_path_string = model_path.string();
            registry.add<commons::renderer>(entity, model_path_string.c_str());
        } else {
            registry.add<commons::renderer>(entity);
        }

        auto renderer = registry.get<commons::renderer>(entity.id);
        if (!renderer)
            return false;

        if (renderer->m_modelo && data.HasMember("materials")) {
            const auto& materials = data["materials"];
            if (materials.Size() != renderer->m_modelo->meshes.size())
                return false;
            for (rapidjson::SizeType index = 0;
                 index < materials.Size();
                 ++index) {
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
                renderer->m_modelo->meshes[index].setMaterial(material);
            }
        }
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
        const std::filesystem::path script_path(script->file);
        script->arquivoCompleto = (
            script_path.is_absolute()
                ? script_path
                : project_directory / script_path
        ).lexically_normal().string();
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
        return registry.get<commons::camera>(entity.id)->analyze(data);
    }

    if (name == "directional_light") {
        registry.add<commons::directional_light>(entity);
        return registry.get<commons::directional_light>(entity.id)->analyze(data);
    }

    return false;
}

bool validate_entities(const rapidjson::Value& entities, std::string& error) {
    if (!entities.IsArray()) {
        error = "The scene does not contain a valid entity list.";
        return false;
    }
    std::unordered_map<std::uint32_t, bool> entity_ids;
    for (const auto& entity : entities.GetArray()) {
        if (!entity.IsObject() ||
            !entity.HasMember("id") ||
            !entity["id"].IsUint() ||
            entity["id"].GetUint() == 0 ||
            (entity.HasMember("name") && !entity["name"].IsString()) ||
            (entity.HasMember("locked") && !entity["locked"].IsBool()) ||
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

bool validate_project(const rapidjson::Document& document, std::string& error) {
    if (!document.IsObject() ||
        !document.HasMember("format") ||
        !document["format"].IsString() ||
        std::string(document["format"].GetString()) != "bubble-engine-project" ||
        !document.HasMember("version") ||
        !document["version"].IsUint() ||
        document["version"].GetUint() != 1 ||
        (!document.HasMember("scene") && !document.HasMember("entities"))) {
        error = "The file is not a supported Bubble Engine project.";
        return false;
    }

    if (document.HasMember("settings") &&
        (!document["settings"].IsObject() ||
         (document["settings"].HasMember("gravity") &&
          !is_vector3(document["settings"]["gravity"])))) {
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

bool write_document(
    const std::filesystem::path& target,
    const rapidjson::Document& document,
    const char* document_type,
    std::string& error
) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    const auto temp_path = target.parent_path() /
        (target.filename().string() + ".tmp");
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "Could not open " + std::string(document_type) +
            " file '" + temp_path.string() + "' for writing.";
        return false;
    }
    output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    output.flush();
    if (!output) {
        output.close();
        std::error_code ignore;
        std::filesystem::remove(temp_path, ignore);
        error = "Could not finish writing " + std::string(document_type) +
            " file '" + target.string() + "'.";
        return false;
    }
    output.close();
    if (output.fail()) {
        std::error_code ignore;
        std::filesystem::remove(temp_path, ignore);
        error = "Could not finalize " + std::string(document_type) +
            " file '" + target.string() + "' after write.";
        return false;
    }

    std::error_code rename_error;
    std::filesystem::rename(temp_path, target, rename_error);
    if (rename_error) {
        std::error_code remove_error;
        std::filesystem::remove(temp_path, remove_error);
        error = "Could not replace " + std::string(document_type) +
            " file '" + target.string() + "': " + rename_error.message();
        return false;
    }
    return true;
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
    error.clear();
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
        if (m_project_path == absolute_target && !m_scene_path.empty()) {
            scene_target = m_scene_path;
        } else if (!m_asset_root.empty() &&
                   absolute_target.parent_path() == m_asset_root) {
            scene_target = m_asset_root / "main.scene";
        } else {
            scene_target = absolute_target.parent_path() /
                (absolute_target.stem().string() + ".scene");
        }
        if (!m_asset_root.empty() &&
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

        if (m_entity_names) {
            const auto name_it = m_entity_names->find(id);
            if (name_it != m_entity_names->end() && !name_it->second.empty()) {
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
                    const auto albedo = material_albedo(mesh.getMaterial());
                    rapidjson::Value material(rapidjson::kObjectType);
                    rapidjson::Value color(rapidjson::kArrayType);
                    color.PushBack(albedo.r, allocator);
                    color.PushBack(albedo.g, allocator);
                    color.PushBack(albedo.b, allocator);
                    color.PushBack(albedo.a, allocator);
                    material.AddMember("albedo", color, allocator);
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
    if (!write_document(scene_target, scene_document, "scene", error))
        return false;

    if (writing_scene) {
        m_scene_path = absolute_target;
        return true;
    }

    rapidjson::Document project_document;
    project_document.SetObject();
    auto& project_allocator = project_document.GetAllocator();
    rapidjson::Value project_format;
    project_format.SetString("bubble-engine-project", project_allocator);
    project_document.AddMember("format", project_format, project_allocator);
    project_document.AddMember("version", 1, project_allocator);

    rapidjson::Value settings(rapidjson::kObjectType);
    rapidjson::Value gravity(rapidjson::kArrayType);
    for (const auto axis : m_global_gravity)
        gravity.PushBack(axis, project_allocator);
    settings.AddMember("gravity", gravity, project_allocator);
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

    if (!write_document(
            absolute_target,
            project_document,
            "project",
            error
        ))
        return false;

    m_project_path = absolute_target;
    m_scene_path = scene_target;
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
    if (is_project_document) {
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
            std::ifstream scene_input(staged_scene_path, std::ios::binary);
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
    if (document.HasMember("settings") &&
        document["settings"].HasMember("gravity")) {
        const auto& gravity = document["settings"]["gravity"];
        for (rapidjson::SizeType axis = 0; axis < gravity.Size(); ++axis)
            staged_gravity[axis] = gravity[axis].GetFloat();
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

    auto staged_registry = std::make_shared<commons::ecs>();
    entity_name_map staged_entity_names;
    entity_state_set staged_hidden_entities;
    entity_state_set staged_locked_entities;
    for (const auto& entity_data : (*scene_data)["entities"].GetArray()) {
        const auto id = entity_data["id"].GetUint();
        if (id == m_editor_camera_id)
            continue;

        auto entity = staged_registry->create(id);
        if (entity_data.HasMember("name") && entity_data["name"].IsString())
            staged_entity_names.emplace(id, entity_data["name"].GetString());
        if (entity_data.HasMember("locked") &&
            entity_data["locked"].GetBool()) {
            staged_locked_entities.insert(id);
        }
        const auto& components = entity_data["components"];
        for (auto component = components.MemberBegin(); component != components.MemberEnd(); ++component) {
            const std::string name(component->name.GetString(), component->name.GetStringLength());
            if (!parse_component(
                    name,
                    component->value,
                    *staged_registry,
                    entity,
                    project_directory
                )) {
                error = "Could not load component '" + name +
                    "' on entity " + std::to_string(id) + ".";
                return false;
            }
        }
        const auto renderer = staged_registry->get<commons::renderer>(id);
        if (renderer && !renderer->visible)
            staged_hidden_entities.insert(id);
    }

    auto editor_camera_entity = staged_registry->create(m_editor_camera_id);
    staged_registry->add<commons::camera>(editor_camera_entity);
    auto& editor_components = staged_registry->entities[m_editor_camera_id];
    editor_components[commons::transform::mask] = active_transform;
    editor_components[commons::camera::mask] = active_camera;

    auto staged_registry_copy = std::make_shared<commons::ecs>();
    staged_registry_copy->entities = staged_registry->entities;
    staged_registry_copy->mascaras = staged_registry->mascaras;
    staged_registry_copy->live_entities = staged_registry->live_entities;
    for (auto& [id, components] : staged_registry_copy->entities) {
        for (auto& [mask, component] : components) {
            (void)mask;
            component->reg = staged_registry_copy;
            component->my_object = id;
        }
    }

    auto staged_scene_entity_names = staged_entity_names;
    const auto staged_asset_root = project_directory.lexically_normal();
    const auto staged_global_gravity = staged_gravity;
    const auto staged_project_path = is_project_document
        ? absolute_project_path
        : std::filesystem::path{};

    m_registry->entities = std::move(staged_registry_copy->entities);
    m_registry->mascaras = std::move(staged_registry_copy->mascaras);
    m_registry->live_entities = std::move(staged_registry_copy->live_entities);
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

    return true;
}

}
