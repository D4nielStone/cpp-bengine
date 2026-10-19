#pragma once

#include "core/ecs.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace editor::ui {

class project_persistence_system final {
public:
    project_persistence_system(
        std::shared_ptr<commons::ecs> registry,
        std::uint32_t editor_camera_id
    );

    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;

private:
    std::shared_ptr<commons::ecs> m_registry;
    std::uint32_t m_editor_camera_id;
};

}
