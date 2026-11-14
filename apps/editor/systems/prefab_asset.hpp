#pragma once
#include "entity_clipboard.hpp"
#include "project_persistence_system.hpp"
#include "project_file_transaction.hpp"
#include <limits>
#include <set>

namespace editor::ui {
inline bool instantiate_prefab(const std::shared_ptr<editor_context>& context,
    const std::filesystem::path& path, std::string& error) {
    if (!context || context->is_playing) { error = "Pare a execução antes de adicionar objetos."; return false; }
    try {
        rapidjson::Document document;
        document.Parse(project_files::read(path).c_str());
        if (!document.IsObject() || !document.HasMember("prefab_root") || !document["prefab_root"].IsUint())
            throw std::runtime_error("Arquivo .bobject sem entidade raiz válida.");
        auto source = std::make_shared<editor_context>(std::make_shared<commons::ecs>());
        source->project_root = context->project_root;
        const auto camera_id = std::numeric_limits<std::uint32_t>::max();
        auto camera = source->registry->create(camera_id);
        source->registry->add<commons::camera>(camera);
        source->editor_camera_entity = camera.id;
        project_persistence_system loader(source->registry, camera.id, &source->entity_names,
            &source->hidden_entities, &source->locked_entities);
        loader.set_migration_writes_enabled(false);
        if (!loader.load(path.string(), error)) return false;
        source->selected_entity = document["prefab_root"].GetUint();
        if (!source->registry->is_alive(*source->selected_entity))
            throw std::runtime_error("Entidade raiz do prefab ausente.");
        auto root_transform = source->registry->get<commons::transform>(*source->selected_entity);
        if (!root_transform || root_transform->parent_id)
            throw std::runtime_error("A raiz do prefab não pode ter um pai externo.");
        std::set<std::uint32_t> descendants{*source->selected_entity};
        bool changed = true;
        while (changed) {
            changed = false;
            for (const auto id : source->registry->live_entities) {
                if (id == camera.id) continue;
                auto transform = source->registry->get<commons::transform>(id);
                if (transform && descendants.contains(transform->parent_id) && descendants.insert(id).second) changed = true;
            }
        }
        if (descendants.size() + 1 != source->registry->live_entities.size())
            throw std::runtime_error("O prefab contém entidades fora da sua hierarquia raiz.");
        entity_clipboard clipboard;
        return clipboard.copy(source, error) && clipboard.paste(context, error);
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}
}
