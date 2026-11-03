#pragma once

#include <memory>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include <functional>

#include "core/ecs.hpp"
#include "os/time.hpp"
#include "systems/system.hpp"

struct GLFWwindow;

namespace bgui {
    class scoped_interface;
}

namespace COMMONS_NS {
    namespace detail { class file_drop_bridge; }
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
        // Execute once between UI frames, on the context owner thread.
        void defer(std::function<void()> task) { m_deferred.push_back(std::move(task)); }
        void set_close_request_callback(std::function<void()> callback);
        void close();
        void reopen();
        void resize(int width, int height);
        using file_drop_callback = std::function<void(const std::vector<std::filesystem::path>&, int, int)>;
        void set_file_drop_callback(file_drop_callback callback);
        bool file_drag_hovered() const;

    private:
        static void close_callback(GLFWwindow* window);
        static void drop_callback(GLFWwindow* window, int count, const char** paths);
        void handle_close_request();

        GLFWwindow* m_window{nullptr};
        std::unique_ptr<bgui::scoped_interface> m_interface;
        std::shared_ptr<ecs> m_ecs;
        std::vector<std::shared_ptr<system>> m_systems;
        std::function<void()> m_close_request_callback;
        struct pending_drop {
            std::vector<std::filesystem::path> paths;
            int x, y;
        };
        file_drop_callback m_file_drop_callback;
        std::unique_ptr<detail::file_drop_bridge> m_file_drop_bridge;
        std::vector<pending_drop> m_pending_drops;
        time m_time;
        bool m_looping{false};
        std::vector<std::function<void()>> m_deferred;
    };
}
