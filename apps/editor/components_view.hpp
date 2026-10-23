#pragma once

#include "editor_context.hpp"
#include "panel.hpp"

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

#include <bgui.hpp>
#include <elem/checkbox.hpp>
#include <elem/combo_box.hpp>
#include <elem/field.hpp>
#include <elem/icon.hpp>
#include <elem/inputbox.hpp>
#include <elem/tree.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <charconv>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace editor::ui {
namespace detail {

class refreshing_layout final : public bgui::linear {
public:
    explicit refreshing_layout(const bgui::orientation& orientation)
        : bgui::linear(orientation) {}

    std::function<void()> before_update;

    void on_update() override {
        if (before_update)
            before_update();
        bgui::linear::on_update();
    }
};

}

class components_view final : public panel {
public:
    explicit components_view(std::shared_ptr<editor_context> context)
        : m_context(std::move(context)) {}

    components_view(const components_view&) = delete;
    components_view& operator=(const components_view&) = delete;

    void build(bgui::dock& parent) override {
        if (m_window)
            return;

        m_window = &parent.add_window(
            "Inspector",
            bgui::dock_area::right,
            0.3f
        );
        m_root = &m_window->add<detail::refreshing_layout>(
            bgui::orientation::vertical
        );
        m_root->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::match_parent
        );
        m_root->style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::start
        };
        m_root->set_scrollable(true);
        m_root->add_class("editor-panel");
        m_root->add_class("components-view");
        m_root->before_update = [this]() { refresh_if_needed(); };

        m_toolbar = &m_root->add<bgui::linear>(
            bgui::orientation::horizontal
        );
        m_toolbar->add_class("entities-toolbar");
        m_toolbar->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_toolbar->style.layout.padding = bgui::vec4i{4, 3, 4, 3};

        m_refresh_requested = true;
    }

    void update() override {
        refresh_if_needed();
    }

    void refresh() {
        m_refresh_requested = true;
    }

private:
    using entity_id = editor_context::entity_id;

    std::shared_ptr<editor_context> m_context;
    bgui::window* m_window{nullptr};
    detail::refreshing_layout* m_root{nullptr};
    bgui::linear* m_toolbar{nullptr};
    bgui::tree* m_entity_tree{nullptr};
    std::optional<entity_id> m_rendered_selection;
    bool m_refresh_requested{false};

    static std::string format_value(const float value) {
        auto result = std::to_string(value);
        result.erase(result.find_last_not_of('0') + 1);
        if (!result.empty() && result.back() == '.')
            result.pop_back();
        return result;
    }

    static void configure_row(bgui::linear& row) {
        row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        row.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::center
        };
    }

    void clear_content() {
        m_entity_tree = nullptr;
        for (auto& [layer, elements] : m_root->get_elements()) {
            (void)layer;
            while (elements.size() > 1)
                m_root->remove(elements.back().get());
        }
    }

    void refresh_if_needed() {
        if (!m_root)
            return;

        auto selected = m_context
            ? m_context->selected_entity
            : std::optional<entity_id>{};

        if (selected && (!m_context->registry ||
            m_context->registry->entities.find(*selected) ==
                m_context->registry->entities.end())) {
            m_context->selected_entity.reset();
            selected.reset();
        }

        if (!m_refresh_requested && selected == m_rendered_selection)
            return;

        m_refresh_requested = false;
        m_rendered_selection = selected;
        clear_content();
        clear_toolbar();

        if (!selected) {
            auto& message = m_root->add<bgui::text>(
                "Select an entity to inspect its components.",
                0.32f
            );
            message.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
            return;
        }

        build_entity(*selected);
    }

    void build_entity(const entity_id id) {
        const auto registry = m_context->registry;
        build_toolbar(id);
        m_entity_tree = &m_root->add<bgui::tree>(
            entity_name(id),
            0.34f,
            true
        );
        m_entity_tree->add_class("editor-header");
        m_entity_tree->set_icon("home", 0.34f);
        m_entity_tree->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );

        auto& id_label = m_entity_tree->children().add<bgui::text>(
            "ID: " + std::to_string(id),
            0.27f
        );
        id_label.add_class("editor-property-label");
        id_label.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );

        if (auto transform = registry->get<commons::transform>(id))
            add_transform(transform);

        if (auto renderer = registry->get<commons::renderer>(id))
            add_renderer(id, renderer);

        if (auto camera = registry->get<commons::camera>(id))
            add_camera(id, camera);

        if (auto light = registry->get<commons::directional_light>(id))
            add_directional_light(id, light);

    }

    std::string entity_name(const entity_id id) const {
        const auto name = m_context->entity_names.find(id);
        if (name != m_context->entity_names.end() && !name->second.empty())
            return name->second;
        return "Entity " + std::to_string(id);
    }

    void clear_toolbar() {
        if (!m_toolbar)
            return;
        for (auto& [layer, elements] : m_toolbar->get_elements()) {
            (void)layer;
            while (!elements.empty())
                m_toolbar->remove(elements.back().get());
        }
    }

    void build_toolbar(const entity_id id) {
        std::vector<std::string> options{"Add Component"};
        const auto registry = m_context->registry;
        if (!registry->get<commons::camera>(id))
            options.emplace_back("Camera");
        if (!registry->get<commons::directional_light>(id))
            options.emplace_back("Directional Light");

        auto& component_combo = m_toolbar->add<bgui::combo_box>(
            options,
            0.28f
        );
        component_combo.add_class("editor-input");
        component_combo.style.layout.require_mode(
            bgui::mode::wrap_content,
            bgui::mode::match_parent
        );
        component_combo.set_on_change(
            [this, id](const std::size_t index, const std::string& option) {
                (void)index;
                if (option == "Camera")
                    add_component<commons::camera>(id);
                else if (option == "Directional Light")
                    add_component<commons::directional_light>(id);
            }
        );

        auto& name_icon = m_toolbar->add<bgui::icon>("edit", 0.24f);
        name_icon.receives_input(false);
        name_icon.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::wrap_content
        );
        name_icon.style.layout.require_width(bgui::mode::pixel, 18.f);
        name_icon.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };

        auto& name_input = m_toolbar->add<bgui::inputbox>(
            entity_name(id),
            "Entity name...",
            0.3f,
            [this, id](const std::string& name) {
                if (!name.empty())
                    m_context->entity_names[id] = name;
                else
                    m_context->entity_names.erase(id);
                refresh();
            }
        );
        name_input.add_class("editor-input");
        name_input.style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::wrap_content
        );
    }

    bgui::tree& add_section(
        const std::string& name,
        const bool removable,
        const std::function<void()>& remove_component
    ) {
        auto& section = m_entity_tree->add_child(name);
        section.set_expanded(true);
        section.add_class("editor-section");
        section.set_icon("circle", 0.27f);
        section.children().add_class("editor-surface");
        section.children().style.layout.padding = bgui::vec4i{6, 6, 6, 6};
        section.children().style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::start
        };

        if (removable) {
            auto& remove_row = section.children().add<bgui::linear>(
                bgui::orientation::horizontal
            );
            remove_row.add_class("editor-property-row");
            configure_row(remove_row);
            remove_row.add<bgui::icon>("trash_alt", 0.27f);
            auto& remove_button = remove_row.add<bgui::button>(
                "Remove Component",
                0.27f,
                remove_component
            );
            remove_button.add_class("editor-input");
            remove_button.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );
        }

        return section;
    }

    template<typename Component>
    void remove_component(const entity_id id) {
        if (!m_context || !m_context->registry)
            return;
        m_context->registry->remove<Component>(id);
        refresh();
    }

    void add_transform(const std::shared_ptr<commons::transform>& transform) {
        auto& section = add_section("Transform", false, {});
        auto& content = section.children();

        add_vector_field(
            content,
            "Position",
            transform->get_position(),
            [transform](std::size_t axis, float value) {
                auto position = transform->get_position();
                position[axis] = value;
                transform->set_position(position);
            }
        );
        add_vector_field(
            content,
            "Rotation",
            transform->get_rotation(),
            [transform](std::size_t axis, float value) {
                auto rotation = transform->get_rotation();
                rotation[axis] = value;
                transform->set_rotation(rotation);
            }
        );
        add_vector_field(
            content,
            "Scale",
            transform->get_scale(),
            [transform](std::size_t axis, float value) {
                auto scale = transform->get_scale();
                scale[axis] = value;
                transform->set_scale(scale);
            }
        );
    }

    void add_renderer(
        const entity_id id,
        const std::shared_ptr<commons::renderer>& renderer
    ) {
        auto& section = add_section(
            "Renderer",
            true,
            [this, id]() { remove_component<commons::renderer>(id); }
        );
        auto& icon_row = section.children().add<bgui::linear>(
            bgui::orientation::horizontal
        );
        configure_row(icon_row);
        icon_row.add<bgui::icon>("file", 0.27f);
        const auto source = renderer->m_modelo
            ? renderer->m_modelo->get_source_path()
            : std::string{"No model assigned"};
        auto& source_label = icon_row.add<bgui::text>(source, 0.27f);
        source_label.style.layout.require_width(bgui::mode::stretch);
    }

    void add_camera(
        const entity_id id,
        const std::shared_ptr<commons::camera>& camera
    ) {
        auto& section = add_section(
            "Camera",
            true,
            [this, id]() { remove_component<commons::camera>(id); }
        );
        auto& content = section.children();

        add_number_field(
            content,
            "Field of View",
            camera->fov,
            [camera](float value) { camera->fov = value; }
        );
        add_number_field(
            content,
            "Far Clip",
            camera->corte_longo,
            [camera](float value) { camera->corte_longo = value; }
        );
        add_number_field(
            content,
            "Orthographic Scale",
            camera->scale,
            [camera](float value) { camera->scale = value; }
        );

        auto& orthographic = content.add<bgui::checkbox>(
            "Orthographic",
            0.29f,
            camera->flag_orth
        );
        orthographic.set_on_change(
            [camera](bool enabled) { camera->flag_orth = enabled; }
        );

        auto& show_frustum = content.add<bgui::checkbox>(
            "Show Frustum",
            0.29f,
            m_context->show_camera_frustum
        );
        show_frustum.set_on_change([context = m_context](bool enabled) {
            context->show_camera_frustum = enabled;
        });
    }

    void add_directional_light(
        const entity_id id,
        const std::shared_ptr<commons::directional_light>& light
    ) {
        auto& section = add_section(
            "Directional Light",
            true,
            [this, id]() {
                remove_component<commons::directional_light>(id);
            }
        );
        auto& content = section.children();

        add_vector_field(
            content,
            "Direction",
            light->direction,
            [light](std::size_t axis, float value) {
                light->direction[axis] = value;
            }
        );
        add_vector_field(
            content,
            "Ambient",
            light->ambient,
            [light](std::size_t axis, float value) {
                light->ambient[axis] = value;
            }
        );
        add_vector_field(
            content,
            "Color",
            light->color,
            [light](std::size_t axis, float value) {
                light->color[axis] = value;
            }
        );
        add_number_field(
            content,
            "Intensity",
            light->intensity,
            [light](float value) { light->intensity = value; }
        );
    }

    void add_vector_field(
        bgui::linear& parent,
        const std::string& label,
        const bgui::vec3& value,
        const std::function<void(std::size_t, float)>& on_change
    ) {
        auto& name = parent.add<bgui::text>(label, 0.29f);
        name.add_class("editor-property-label");
        name.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );

        auto& values = parent.add<bgui::linear>(
            bgui::orientation::horizontal
        );
        values.add_class("editor-property-row");
        configure_row(values);

        constexpr const char* axes[]{"X", "Y", "Z"};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto& axis_label = values.add<bgui::text>(axes[axis], 0.27f);
            axis_label.add_class("editor-property-label");
            axis_label.style.layout.require_width(bgui::mode::pixel, 12.f);

            auto& input = values.add<bgui::inputbox>(
                format_value(value[axis]),
                "",
                0.27f,
                [on_change, axis, label](const std::string& buffer) {
                    float parsed{};
                    const auto result = std::from_chars(
                        buffer.data(),
                        buffer.data() + buffer.size(),
                        parsed,
                        std::chars_format::general
                    );
                    if (result.ec != std::errc{} ||
                        result.ptr != buffer.data() + buffer.size() ||
                        !std::isfinite(parsed)) {
                        std::cerr << "[editor] Invalid number for "
                                  << label << ": " << buffer << '\n';
                        return;
                    }
                    on_change(axis, parsed);
                },
                bgui::input_mode::inputbox
            );
            input.add_class("editor-input");
            input.style.layout.require_width(bgui::mode::stretch);
            input.style.layout.require_height(bgui::mode::pixel, 22.f);
        }
    }

    void add_number_field(
        bgui::linear& parent,
        const std::string& label,
        const float value,
        const std::function<void(float)>& on_change
    ) {
        auto& field = parent.add<bgui::field>(
            label,
            format_value(value),
            0.29f,
            [on_change, label](const std::string& buffer) {
                float parsed{};
                const auto result = std::from_chars(
                    buffer.data(),
                    buffer.data() + buffer.size(),
                    parsed,
                    std::chars_format::general
                );
                if (result.ec != std::errc{} ||
                    result.ptr != buffer.data() + buffer.size() ||
                    !std::isfinite(parsed)) {
                    std::cerr << "[editor] Invalid number for "
                              << label << ": " << buffer << '\n';
                    return;
                }
                on_change(parsed);
            }
        );
        field.add_class("editor-input");
        field.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
    }

    template<typename Component>
    void add_component(const entity_id id) {
        if (!m_context || !m_context->registry)
            return;
        auto entity = commons::entity{
            id,
            m_context->registry->get_components(id)
        };
        m_context->registry->add<Component>(entity);
        refresh();
    }
};

}
