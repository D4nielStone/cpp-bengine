#pragma once
#include "editor_context.hpp"
#include "components/renderer.hpp"
#include "primitive_catalog.hpp"
#include <functional>

namespace editor::ui {
using primitive_loader = std::function<std::unique_ptr<commons::model>(const std::filesystem::path&)>;
inline std::optional<editor_context::entity_id> create_primitive_entity(
    const std::shared_ptr<editor_context>& context, size_t index,
    const primitive_loader& loader = [](const auto& path) { return std::make_unique<commons::model>(path.string()); }) {
    if (!context || !context->registry || context->is_playing || index >= primitive_entities.size()) return std::nullopt;
    const auto& primitive = primitive_entities[index];
    auto geometry = loader(std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / primitive.asset);
    if (!geometry || geometry->meshes.empty()) throw std::runtime_error("Não foi possível carregar o modelo primitivo.");
    auto entity = context->registry->create();
    try {
        context->registry->add<commons::renderer>(entity, geometry.get());
        geometry.release();
        context->entity_names[entity.id] = primitive.name;
        context->registry->entity_names[entity.id] = primitive.name;
        context->record_entity_creation(entity.id);
        context->selected_entity = entity.id;
    } catch (...) { context->registry->remove(entity.id); context->entity_names.erase(entity.id); throw; }
    return entity.id;
}
}
