#pragma once

#include <memory>
#include <string>
#include <utility>

#include "core/ecs.hpp"

namespace COMMONS_NS {
    class scene final {
    public:
        explicit scene(std::string name = "Scene")
            : m_name(std::move(name)), m_ecs(std::make_shared<ecs>()) {}

        scene(const scene&) = delete;
        scene& operator=(const scene&) = delete;

        const std::string& get_name() const noexcept {
            return m_name;
        }

        void set_name(std::string name) {
            m_name = std::move(name);
        }

        std::shared_ptr<ecs> get_ecs() const noexcept {
            return m_ecs;
        }

    private:
        std::string m_name;
        std::shared_ptr<ecs> m_ecs;
    };
}