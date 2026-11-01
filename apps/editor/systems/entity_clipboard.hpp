#pragma once
#include "editor_context.hpp"
#include "components/transform.hpp"
#include "components/renderer.hpp"
#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/physics.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/properties.hpp"
#include <map>
#include <set>

namespace editor::ui {
class entity_clipboard {
    using id_type = editor_context::entity_id;
    using factory = std::function<void(commons::ecs&, commons::entity)>;
    struct snapshot {
        id_type id, parent;
        std::string name;
        bool hidden, locked;
        glm::mat4 world;
        std::vector<factory> components;
    };
    std::vector<snapshot> m_entities;
    std::filesystem::path m_project;

    template<class T> static factory serialized(const std::shared_ptr<T>& component) {
        rapidjson::Document value(rapidjson::kObjectType);
        if (!component->serialize(value, value.GetAllocator()))
            throw std::runtime_error("Não foi possível copiar um componente.");
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        value.Accept(writer);
        return [text = std::string(buffer.GetString())](commons::ecs& registry, commons::entity entity) {
            rapidjson::Document data;
            data.Parse(text.c_str());
            if constexpr (std::is_same_v<T, commons::transform>) {
                if (!registry.get<T>(entity.id)->analyze(data)) throw std::runtime_error("Transformação inválida.");
            } else {
                registry.add<T>(entity);
                if (!registry.get<T>(entity.id)->analyze(data)) throw std::runtime_error("Componente inválido.");
            }
        };
    }
public:
    bool copy(const std::shared_ptr<editor_context>& context, std::string& error) {
        error.clear();
        if (!context || !context->registry || context->is_playing || !context->selected_entity ||
            context->selected_entity == context->editor_camera_entity) return false;
        try {
            auto registry = context->registry;
            std::set<id_type> selected{*context->selected_entity};
            bool changed;
            do {
                changed = false;
                for (const auto& [id, components] : registry->entities) {
                    auto transform = registry->get<commons::transform>(id);
                    if (transform && selected.contains(transform->parent_id) && !selected.contains(id) &&
                        id != context->editor_camera_entity) { selected.insert(id); changed = true; }
                }
            } while (changed);
            std::vector<snapshot> pending;
            // The selected root comes first, independently of numeric ID order.
            std::vector<id_type> order{*context->selected_entity};
            for (auto id : selected) if (id != order.front()) order.push_back(id);
            for (auto id : order) {
                auto transform = registry->get<commons::transform>(id);
                if (!transform) throw std::runtime_error("A entidade não possui transformação.");
                snapshot value{id, transform->parent_id, context->entity_names.contains(id) ? context->entity_names.at(id) : "Entity " + std::to_string(id),
                    context->hidden_entities.contains(id), context->locked_entities.contains(id), transform->get_world_model_matrix(), {}};
                for (const auto& [mask, component] : registry->entities.at(id)) {
                    switch (mask) {
                    case commons::transform::mask: {
                        auto source = std::static_pointer_cast<commons::transform>(component);
                        value.components.push_back([position = source->get_position(), rotation = source->get_rotation(),
                            scale = source->get_scale(), up = source->get_up(), target = source->get_target(),
                            use_target = source->is_using_target()](commons::ecs& ecs, commons::entity entity) {
                            auto transform = ecs.get<commons::transform>(entity.id);
                            transform->set_position(position); transform->set_rotation(rotation);
                            transform->set_scale(scale); transform->set_up(up);
                            if (use_target) transform->look_at_vector(target);
                        }); break;
                    }
                    case commons::camera::mask: value.components.push_back(serialized(std::static_pointer_cast<commons::camera>(component))); break;
                    case commons::physics::mask: value.components.push_back(serialized(std::static_pointer_cast<commons::physics>(component))); break;
                    case commons::directional_light::mask: value.components.push_back(serialized(std::static_pointer_cast<commons::directional_light>(component))); break;
                    case commons::spot_light::mask: value.components.push_back(serialized(std::static_pointer_cast<commons::spot_light>(component))); break;
                    case commons::renderer::mask: {
                        const auto source = std::static_pointer_cast<commons::renderer>(component);
                        auto geometry = source->m_modelo ? std::shared_ptr<commons::model>(source->m_modelo->clone()) : nullptr;
                        value.components.push_back([geometry, visible = source->visible, cast = source->cast_shadows,
                            receive = source->receive_shadows](commons::ecs& ecs, commons::entity entity) {
                            auto copy = geometry ? geometry->clone() : nullptr;
                            if (copy && glad_glGetString && glGetString(GL_VERSION))
                                for (auto& mesh : copy->meshes) mesh.load();
                            ecs.add<commons::renderer>(entity, copy.get());
                            copy.release();
                            auto renderer = ecs.get<commons::renderer>(entity.id);
                            renderer->visible = visible; renderer->cast_shadows = cast; renderer->receive_shadows = receive;
                        });
                        break;
                    }
                    case commons::code::mask: {
                        const auto source = std::static_pointer_cast<commons::code>(component);
                        auto create = serialized(source);
                        value.components.push_back([create, root = source->asset_root](commons::ecs& ecs, commons::entity entity) {
                            create(ecs, entity);
                            auto code = ecs.get<commons::code>(entity.id);
                            code->asset_root = root;
                            code->arquivoCompleto = (std::filesystem::path(code->file).is_absolute() ?
                                std::filesystem::path(code->file) : std::filesystem::path(root) / code->file).string();
                            for (auto& script : code->scripts) {
                                script->asset_root = root; script->my_object = entity.id; script->reg = code->reg;
                                script->arquivoCompleto = (std::filesystem::path(script->file).is_absolute() ?
                                    std::filesystem::path(script->file) : std::filesystem::path(root) / script->file).string();
                            }
                        });
                        break;
                    }
                    case commons::properties::mask: {
                        auto source = std::static_pointer_cast<commons::properties>(component);
                        value.components.push_back([name = source->nome, tag = source->tag, active = source->ativo](commons::ecs& ecs, commons::entity entity) {
                            ecs.add<commons::properties>(entity, name, tag, active);
                        }); break;
                    }
                    case commons::point_light::mask: {
                        auto source = std::static_pointer_cast<commons::point_light>(component);
                        value.components.push_back([position = source->position, color = source->color,
                            intensity = source->intensity, constant = source->constant, linear = source->linear,
                            quadratic = source->quadratic](commons::ecs& ecs, commons::entity entity) {
                            ecs.add<commons::point_light>(entity, position, color, intensity, constant, linear, quadratic);
                        }); break;
                    }
                    default: throw std::runtime_error("A entidade contém um componente que ainda não suporta cópia.");
                    }
                }
                pending.push_back(std::move(value));
            }
            m_entities = std::move(pending); m_project = context->project_root;
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }

    bool paste(const std::shared_ptr<editor_context>& context, std::string& error) {
        error.clear();
        if (!context || !context->registry || context->is_playing || m_entities.empty()) return false;
        if (context->project_root != m_project) { error = "Copie uma entidade do projeto atual antes de colar."; return false; }
        auto registry = context->registry;
        std::map<id_type, commons::entity> created;
        const auto previously_alive = registry->live_entities;
        const auto previous_selection = context->selected_entity;
        try {
            for (const auto& value : m_entities) created.emplace(value.id, registry->create());
            for (const auto& value : m_entities)
                for (const auto& create : value.components) create(*registry, created.at(value.id));
            for (const auto& value : m_entities) {
                const auto id = created.at(value.id).id;
                auto transform = registry->get<commons::transform>(id);
                if (created.contains(value.parent)) transform->parent_id = created.at(value.parent).id;
                else if (value.parent && previously_alive.contains(value.parent) && registry->get<commons::transform>(value.parent)) transform->parent_id = value.parent;
                else if (value.parent) {
                    glm::vec3 scale, position, skew;
                    glm::vec4 perspective;
                    glm::quat rotation;
                    if (!glm::decompose(value.world, scale, rotation, position, skew, perspective))
                        throw std::runtime_error("Não foi possível restaurar a transformação mundial.");
                    const auto angles = glm::degrees(glm::eulerAngles(rotation));
                    transform->set_position({position.x, position.y, position.z});
                    transform->set_rotation({angles.x, angles.y, angles.z});
                    transform->set_scale({scale.x, scale.y, scale.z});
                }
                if (auto camera = registry->get<commons::camera>(id)) {
                    camera->m_transform = transform.get(); camera->skybox_asset_root = context->project_root.string();
                }
                if (auto physics = registry->get<commons::physics>(id))
                    if (auto renderer = registry->get<commons::renderer>(id)) physics->setModelo(renderer->m_modelo);
                context->entity_names[id] = value.name + " (cópia)";
                registry->entity_names[id] = context->entity_names[id];
                if (value.hidden) context->hidden_entities.insert(id);
                if (value.locked) context->locked_entities.insert(id);
            }
        } catch (const std::exception& e) {
            for (const auto& [old, entity] : created) {
                registry->remove(entity.id); context->entity_names.erase(entity.id);
                context->hidden_entities.erase(entity.id); context->locked_entities.erase(entity.id);
            }
            error = e.what(); return false;
        }
        const auto selected = created.at(m_entities.front().id).id;
        auto saved = std::make_shared<commons::ecs>();
        std::map<id_type, std::string> names;
        std::set<id_type> hidden, locked;
        for (const auto& [old, entity] : created) {
            saved->entities[entity.id] = registry->entities.at(entity.id);
            saved->mascaras[entity.id] = registry->mascaras.at(entity.id);
            names[entity.id] = context->entity_names.at(entity.id);
            if (context->hidden_entities.contains(entity.id)) hidden.insert(entity.id);
            if (context->locked_entities.contains(entity.id)) locked.insert(entity.id);
        }
        std::weak_ptr<editor_context> weak = context;
        context->record_action([weak, saved, previous_selection]() {
            if (auto context = weak.lock()) {
                for (const auto& [id, components] : saved->entities) {
                    context->registry->remove(id); context->entity_names.erase(id);
                    context->hidden_entities.erase(id); context->locked_entities.erase(id);
                }
                context->selected_entity = previous_selection && context->registry->is_alive(*previous_selection)
                    ? previous_selection : std::nullopt;
            }
        }, [weak, saved, names, hidden, locked, selected]() {
            if (auto context = weak.lock()) {
                for (const auto& [id, components] : saved->entities) {
                    context->registry->entities[id] = components;
                    context->registry->mascaras[id] = saved->mascaras.at(id);
                    context->registry->live_entities.insert(id);
                    context->entity_names[id] = names.at(id);
                    context->registry->entity_names[id] = names.at(id);
                }
                context->hidden_entities.insert(hidden.begin(), hidden.end());
                context->locked_entities.insert(locked.begin(), locked.end());
                context->selected_entity = selected;
            }
        });
        context->selected_entity = selected;
        ++context->history_revision;
        return true;
    }
};
}
