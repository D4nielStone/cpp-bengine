#pragma once

#include "panel.hpp"

#include <bgui.hpp>
#include <elem/button.hpp>
#include <elem/text.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>
#include <os/os.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace editor::ui {

class files_view final : public panel {
public:
    using drop_callback =
        std::function<void(const std::filesystem::path&, const bgui::vec2i&)>;

    explicit files_view(std::filesystem::path project_file, drop_callback on_drop = {})
        : m_project_root(resolve_project_root(std::move(project_file))),
          m_on_drop(std::move(on_drop)),
          m_current_directory(m_project_root) {
        m_expanded_directories.insert(m_project_root);
        load_main_scene();
    }

    files_view(const files_view&) = delete;
    files_view& operator=(const files_view&) = delete;

    void build(bgui::dock& parent) override {
        if (m_root)
            return;

        auto& window = parent.add_window(
            "Arquivos",
            bgui::dock_area::bottom,
            0.3f
        );
        m_root = &window.add<bgui::linear>(bgui::orientation::horizontal);
        m_root->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_root->style.layout.padding = bgui::vec4i{0};
        m_root->add_class("editor-panel");

        m_folder_sidebar = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_folder_sidebar->style.layout.require_mode(
            bgui::mode::percent,
            bgui::mode::match_parent
        );
        m_folder_sidebar->style.layout.require_width(
            bgui::mode::percent,
            29.f
        );
        m_folder_sidebar->add_class("entities-list");
        m_folder_sidebar->style.layout.padding = bgui::vec4i{6, 5, 6, 5};
        m_folder_sidebar->style.visual.background.normal =
            bgui::color{0.075f, 0.075f, 0.075f, 1.f};
        m_folder_sidebar->set_scrollable(true);

        m_file_area = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_file_area->style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::match_parent
        );
        m_file_area->style.layout.padding = bgui::vec4i{8, 6, 8, 6};
        m_file_area->style.visual.background.normal =
            bgui::color{0.12f, 0.125f, 0.14f, 1.f};
        m_file_area->set_scrollable(true);

        m_refresh_requested = true;
        refresh_if_needed();
    }

    void update() override {
        refresh_if_needed();
    }

    void set_project_file(const std::filesystem::path& project_file) {
        m_project_root = resolve_project_root(project_file);
        m_current_directory = m_project_root;
        m_selected_file.clear();
        m_main_scene.clear();
        m_expanded_directories.clear();
        m_expanded_directories.insert(m_project_root);
        load_main_scene();
        request_refresh();
    }

private:
    class file_button final : public bgui::button {
    public:
        file_button(
            const std::string& label,
            std::function<void()> on_click,
            std::function<void(const bgui::vec2i&)> on_drop
        )
            : bgui::button(label, 0.28f, on_click),
              m_on_drop(std::move(on_drop)) {}

        void on_pressed() override {
            m_dragged = false;
            m_drag_distance = 0;
            bgui::button::on_pressed();
        }

        void set_drag(const bgui::vec2i& delta) override {
            bgui::button::set_drag(delta);
            if (!m_on_drop)
                return;
            m_drag_distance += std::abs(delta.x) + std::abs(delta.y);
            if (m_drag_distance >= 5)
                m_dragged = true;
        }

        void on_released() override {
            if (!m_dragged) {
                bgui::button::on_released();
                return;
            }

            bgui::element::on_released();
            if (m_on_drop)
                m_on_drop(bgui::get_mouse_position());
        }

    private:
        std::function<void(const bgui::vec2i&)> m_on_drop;
        int m_drag_distance{0};
        bool m_dragged{false};
    };

    std::filesystem::path m_project_root;
    drop_callback m_on_drop;
    std::filesystem::path m_current_directory;
    std::filesystem::path m_selected_file;
    std::filesystem::path m_main_scene;
    std::unordered_set<std::filesystem::path> m_expanded_directories;
    bgui::linear* m_root{nullptr};
    bgui::linear* m_folder_sidebar{nullptr};
    bgui::linear* m_file_area{nullptr};
    bool m_refresh_requested{false};

    static std::filesystem::path resolve_project_root(
        std::filesystem::path project_file
    ) {
        std::error_code error;
        auto absolute_path = std::filesystem::absolute(project_file, error);
        if (error) {
            std::cerr << "[editor] Could not resolve project path: "
                      << error.message() << '\n';
            absolute_path = std::filesystem::current_path();
        }

        if (std::filesystem::is_directory(absolute_path, error) && !error)
            return absolute_path.lexically_normal();

        const auto parent = absolute_path.parent_path();
        return (parent.empty() ? std::filesystem::current_path() : parent)
            .lexically_normal();
    }

    static bool is_scene_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".world" || extension == ".scene";
    }

    static bool is_project_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".bengine";
    }

    static bool is_lua_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".lua";
    }

    static std::string_view file_icon(const std::filesystem::path& path) {
        if (is_project_file(path))
            return "home";
        if (is_scene_file(path))
            return "cube";
        if (is_lua_file(path))
            return "code";
        return "file";
    }

    static bool is_model_file(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        return extension == ".obj" || extension == ".fbx" ||
            extension == ".gltf" || extension == ".glb" ||
            extension == ".dae" || extension == ".3ds";
    }

    static void clear_layout(bgui::linear& layout) {
        for (auto& [layer, elements] : layout.get_elements()) {
            (void)layer;
            while (!elements.empty())
                layout.remove(elements.back().get());
        }
    }

    static void configure_label(bgui::text& label) {
        label.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        label.style.layout.padding = bgui::vec4i{2, 4, 2, 4};
        label.receives_input(false);
    }

    void load_main_scene() {
        m_main_scene.clear();
        std::ifstream marker(m_project_root / ".bengine-main-scene");
        if (!marker)
            return;

        std::string relative_path;
        if (!std::getline(marker, relative_path))
            return;

        m_main_scene = (m_project_root / relative_path).lexically_normal();
    }

    void refresh_if_needed() {
        if (!m_root || !m_refresh_requested)
            return;

        m_refresh_requested = false;
        clear_layout(*m_folder_sidebar);
        clear_layout(*m_file_area);
        build_folder_tree(m_project_root, 0);
        build_file_list();
    }

    void request_refresh() {
        m_refresh_requested = true;
        refresh_if_needed();
    }

    std::string directory_label(const std::filesystem::path& directory) const {
        if (directory == m_project_root)
            return "Projeto";
        return directory.filename().string();
    }

    void build_folder_tree(
        const std::filesystem::path& directory,
        const std::size_t depth
    ) {
        const bool expanded = m_expanded_directories.contains(directory);
        auto& row = m_folder_sidebar->add<bgui::button>(
            "",
            0.27f,
            [this, directory]() {
                if (m_expanded_directories.contains(directory))
                    m_expanded_directories.erase(directory);
                else
                    m_expanded_directories.insert(directory);
                m_current_directory = directory;
                m_selected_file.clear();
                request_refresh();
            }
        );
        row.get_label().set_enable(false);
        auto& icon = row.add<bgui::icon>(
            directory == m_project_root
                ? "home"
                : (expanded ? "folder_open" : "folder"),
            0.25f
        );
        icon.receives_input(false);
        icon.style.layout.require_mode(
            bgui::mode::wrap_content,
            bgui::mode::wrap_content
        );
        icon.style.layout.padding = bgui::vec4i{3, 0, 5, 0};
        auto& label = row.add<bgui::text>(
            (expanded ? "v  " : ">  ") + directory_label(directory),
            0.27f
        );
        label.receives_input(false);
        label.style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::wrap_content
        );
        label.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::center
        };
        row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        row.style.layout.padding = bgui::vec4i{
            static_cast<int>(depth * 12 + 3), 3, 3, 3
        };
        row.style.visual.background.normal =
            directory == m_current_directory
                ? bgui::color{0.19f, 0.21f, 0.24f, 1.f}
                : bgui::color{0.f, 0.f, 0.f, 0.f};

        if (!m_expanded_directories.contains(directory))
            return;

        std::error_code error;
        std::vector<std::filesystem::path> directories;
        for (std::filesystem::directory_iterator it(directory, error), end;
             !error && it != end; it.increment(error)) {
            const auto filename = it->path().filename().string();
            if (filename.empty() || filename.front() == '.')
                continue;
            std::error_code type_error;
            if (it->is_symlink(type_error) || type_error)
                continue;
            type_error.clear();
            if (it->is_directory(type_error) && !type_error)
                directories.push_back(it->path());
        }

        std::sort(directories.begin(), directories.end());
        for (const auto& child : directories)
            build_folder_tree(child, depth + 1);
    }

    void build_file_list() {
        const auto relative_directory =
            m_current_directory.lexically_relative(m_project_root);
        auto& path_label = m_file_area->add<bgui::text>(
            relative_directory.empty() || relative_directory == "."
                ? "Projeto"
                : relative_directory.generic_string(),
            0.32f
        );
        configure_label(path_label);
        path_label.add_class("editor-header");

        if (!m_selected_file.empty() && is_scene_file(m_selected_file)) {
            auto& action = m_file_area->add<bgui::button>(
                m_selected_file == m_main_scene
                    ? "Cena principal"
                    : "Tornar cena principal",
                0.28f,
                [this]() { set_main_scene(); }
            );
            action.style.layout.require_mode(
                bgui::mode::wrap_content,
                bgui::mode::wrap_content
            );
            action.style.layout.padding = bgui::vec4i{7, 3, 7, 3};
            action.set_enable(m_selected_file != m_main_scene);
        }

        std::error_code error;
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(m_current_directory, error), end;
             !error && it != end; it.increment(error)) {
            const auto filename = it->path().filename().string();
            if (filename.empty() || filename.front() == '.')
                continue;
            std::error_code type_error;
            if (it->is_symlink(type_error) || type_error)
                continue;
            type_error.clear();
            if (it->is_regular_file(type_error) && !type_error)
                files.push_back(it->path());
        }

        if (error) {
            add_message("Não foi possível ler esta pasta: " + error.message());
            return;
        }

        std::sort(files.begin(), files.end());
        if (files.empty()) {
            add_message("Esta pasta não contém arquivos.");
            return;
        }
        for (const auto& file : files) {
            const bool selected = file == m_selected_file;
            auto& item = m_file_area->add<file_button>(
                "",
                [this, file]() {
                    m_selected_file = file;
                    request_refresh();
                },
                is_model_file(file)
                    ? [this, file](const bgui::vec2i& position) {
                          if (m_on_drop)
                              m_on_drop(file, position);
                      }
                    : std::function<void(const bgui::vec2i&)>{}
            );
            item.get_label().set_enable(false);
            auto& icon = item.add<bgui::icon>(file_icon(file), 0.27f);
            icon.receives_input(false);
            icon.style.layout.require_mode(
                bgui::mode::wrap_content,
                bgui::mode::wrap_content
            );
            icon.style.layout.padding = bgui::vec4i{2, 0, 6, 0};
            auto& label = item.add<bgui::text>(
                file.filename().string(),
                0.28f
            );
            label.receives_input(false);
            label.set_wrap(false);
            label.style.layout.require_mode(
                bgui::mode::stretch,
                bgui::mode::wrap_content
            );
            label.style.layout.align = bgui::vec<2, bgui::alignment>{
                bgui::alignment::start,
                bgui::alignment::center
            };
            item.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
            item.style.layout.padding = bgui::vec4i{5, 3, 5, 3};
            item.style.visual.background.normal = selected
                ? bgui::color{0.20f, 0.31f, 0.43f, 1.f}
                : bgui::color{0.15f, 0.16f, 0.18f, 1.f};
            item.style.visual.background.hover =
                bgui::color{0.22f, 0.24f, 0.27f, 1.f};
            item.style.visual.border_radius = 3.f;
        }
    }

    void add_message(const std::string& message) {
        auto& label = m_file_area->add<bgui::text>(message, 0.28f);
        configure_label(label);
    }

    void set_main_scene() {
        if (m_selected_file.empty() || !is_scene_file(m_selected_file))
            return;

        const auto relative_path =
            m_selected_file.lexically_relative(m_project_root);
        std::ofstream marker(
            m_project_root / ".bengine-main-scene",
            std::ios::trunc
        );
        if (!marker) {
            std::cerr << "[editor] Could not save the main scene selection.\n";
            return;
        }

        marker << relative_path.generic_string() << '\n';
        if (!marker) {
            std::cerr << "[editor] Could not write the main scene selection.\n";
            return;
        }

        m_main_scene = m_selected_file;
        request_refresh();
    }
};

}
