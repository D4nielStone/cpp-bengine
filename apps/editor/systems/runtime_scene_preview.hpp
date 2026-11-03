#pragma once

#include "components/camera.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include "project_file_transaction.hpp"
#include <cmath>
#include <optional>

namespace editor::ui {

// Runtime preview data is transient and never written into the user's scene.
inline std::string capture_runtime_preview(const std::shared_ptr<commons::ecs>& registry,
    std::optional<std::uint32_t> excluded = {}) {
    rapidjson::Document doc(rapidjson::kArrayType);
    auto& allocator = doc.GetAllocator();
    registry->cada<commons::transform>([&](std::uint32_t id) {
        if (excluded == id) return;
        rapidjson::Value entry(rapidjson::kObjectType), transform(rapidjson::kObjectType);
        entry.AddMember("id", id, allocator);
        registry->get<commons::transform>(id)->serialize(transform, allocator);
        entry.AddMember("transform", transform, allocator);
        if (auto camera = registry->get<commons::camera>(id)) {
            rapidjson::Value config(rapidjson::kObjectType);
            camera->serialize(config, allocator);
            entry.AddMember("camera", config, allocator);
        }
        doc.PushBack(entry, allocator);
    });
    return project_files::encode(doc);
}

inline bool apply_runtime_preview(const std::string& text,
    const std::shared_ptr<commons::ecs>& registry, std::optional<std::uint32_t> excluded = {}) {
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    if (doc.HasParseError() || !doc.IsArray()) return false;
    for (const auto& entry : doc.GetArray()) {
        if (!entry.IsObject() || !entry.HasMember("id") || !entry["id"].IsUint() ||
            !entry.HasMember("transform") || !entry["transform"].IsObject()) return false;
        const auto& transform = entry["transform"];
        for (auto key : {"position", "rotation", "scale"}) {
            if (!transform.HasMember(key) || !transform[key].IsArray() || transform[key].Size() != 3) return false;
            for (auto& number : transform[key].GetArray())
                if (!number.IsNumber() || !std::isfinite(number.GetFloat())) return false;
        }
    }
    for (const auto& entry : doc.GetArray()) {
        const auto id = entry["id"].GetUint();
        if (id == excluded || !registry->is_alive(id)) continue;
        if (auto transform = registry->get<commons::transform>(id)) transform->analyze(entry["transform"]);
        if (entry.HasMember("camera") && entry["camera"].IsObject())
            if (auto camera = registry->get<commons::camera>(id)) camera->analyze(entry["camera"]);
    }
    return true;
}

}
