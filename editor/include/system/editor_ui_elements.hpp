#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include <bgui.hpp>

namespace editor::ui_elements {
    inline constexpr char entities_list[] = "editor-entities-list";
    inline constexpr char components_list[] = "editor-components-list";
    inline constexpr char scene_view_window[] = "editor-scene-view-window";
    inline constexpr char scene_view_context[] = "editor-scene-view-context";
    inline constexpr char framebuffer_image[] = "editor-framebuffer-image";
    inline constexpr char scene_file_dialog[] = "editor-scene-file-dialog";
    inline constexpr char scene_file_input[] = "editor-scene-file-input";
    inline constexpr char scene_file_status[] = "editor-scene-file-status";
    inline constexpr char model_import_status[] = "editor-model-import-status";
    inline constexpr char project_status[] = "editor-project-status";
    inline constexpr char project_scenes_list[] = "editor-project-scenes-list";
    inline constexpr char camera_settings_window[] = "editor-camera-settings-window";
    inline constexpr char console_log[] = "editor-console-log";

    template<typename T>
    std::optional<std::reference_wrapper<T>> find_in(
        bgui::layout& layout,
        const std::string& class_name)
    {
        for (auto& [layer, elements] : layout.get_elements()) {
            (void)layer;
            for (auto& element : elements) {
                if (!element)
                    continue;

                if (element->has_class(class_name)) {
                    if (auto* match = dynamic_cast<T*>(element.get()))
                        return std::ref(*match);
                }

                if (auto* child_layout = element->as_layout()) {
                    if (auto match = find_in<T>(*child_layout, class_name))
                        return match;
                }
            }
        }
        return std::nullopt;
    }

    template<typename T>
    std::optional<std::reference_wrapper<T>> find(const std::string& class_name) {
        return find_in<T>(bgui::get_layout(), class_name);
    }

    template<typename T>
    T& require(const std::string& class_name) {
        auto match = find<T>(class_name);
        if (!match)
            throw std::logic_error("Editor UI element not found: " + class_name);
        return match->get();
    }

    inline void set_text(const std::string& class_name, const std::string& value) {
        require<bgui::text>(class_name).set_buffer(value);
    }

    inline bool contains(const std::string& class_name) {
        return find<bgui::element>(class_name).has_value();
    }
}
