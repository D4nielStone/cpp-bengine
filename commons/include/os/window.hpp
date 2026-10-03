#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/ecs.hpp"
#include "os/time.hpp"
#include "systems/system.hpp"

struct GLFWwindow;

namespace bgui {
    class scoped_interface;
}

namespace COMMONS_NS {
    class window final {
    public:
        window(const std::string& title, int width = 1200, int height = 600);
        ~window();

        window(const window&) = delete;
        window& operator=(const window&) = delete;
        window(window&&) = delete;
        window& operator=(window&&) = delete;

        void add(const std::shared_ptr<system>& system);

        template <typename T, typename... Args>
        std::shared_ptr<T> add(Args&&... args) {
            auto instance = std::make_shared<T>(std::forward<Args>(args)...);
            add(instance);
            return instance;
        }

        std::shared_ptr<ecs> get_ecs() const noexcept;
        double delta_time() const noexcept;
        void refresh();
        void loop();

    private:
        GLFWwindow* m_window{nullptr};
        std::unique_ptr<bgui::scoped_interface> m_interface;
        std::shared_ptr<ecs> m_ecs;
        std::vector<std::shared_ptr<system>> m_systems;
        time m_time;
        bool m_looping{false};
    };
} 