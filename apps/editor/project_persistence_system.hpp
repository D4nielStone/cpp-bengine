#pragma once

#include "core/ecs.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace editor::ui {

class project_persistence_system final {
public:
    using entity_name_map = std::unordered_map<std::uint32_t, std::string>;

    project_persistence_system(
        std::shared_ptr<commons::ecs> registry,
        std::uint32_t editor_camera_id,
        entity_name_map* entity_names = nullptr
    );

    void set_entity_names(entity_name_map* entity_names);

    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;

private:
    std::shared_ptr<commons::ecs> m_registry;
    std::uint32_t m_editor_camera_id;
    entity_name_map* m_entity_names;
};

}
