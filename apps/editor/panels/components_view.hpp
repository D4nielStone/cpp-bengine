#pragma once
#include "../systems/asset_import.hpp"

#include "editor_context.hpp"
#include "panel.hpp"

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "components/spot_light.hpp"
#include "components/sound_listener.hpp"
#include "components/sound_player.hpp"
#include "util/pbr_material.hpp"
#include "script_tools.hpp"

#include <bgui.hpp>
#include <elem/checkbox.hpp>
#include <elem/combo_box.hpp>
#include <elem/color_picker.hpp>
#include "mesh_material_editor.hpp"
#include <elem/field.hpp>
#include <elem/icon.hpp>
#include <elem/image.hpp>
#include <elem/inputbox.hpp>
#include <elem/tree.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <lay/linear.hpp>

#include <charconv>
#include <algorithm>
#include <filesystem>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor::ui {
namespace detail {

class refreshing_layout final : public bgui::linear {
public:
    explicit refreshing_layout(const bgui::orientation& orientation)
        : bgui::linear(orientation) {}

    std::function<void()> before_update;
    std::shared_ptr<bool> alive;

    ~refreshing_layout() override {
        if (alive)
            *alive = false;
    }

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
        m_root->before_update = [this]() { update_inspector(); };
        m_root->alive = m_surface_alive;

        m_toolbar = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_toolbar->add_class("entities-toolbar");
        m_toolbar->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_toolbar->style.layout.padding = bgui::vec4i{4, 3, 4, 3};

        m_refresh_requested = true;
    }

    void capture_session() { if (m_root) remember_section_expansion(); }
    void restore_session() {
        if (m_root) for (auto& [layer, elements] : m_root->get_elements())
            for (auto& element : elements)
                if (auto* section = dynamic_cast<bgui::tree*>(element.get())) {
                    const auto it = m_context->inspector_sections.find(section->label().get_buffer());
                    section->set_expanded(it == m_context->inspector_sections.end() || it->second);
                }
        refresh();
    }

    void update() override {
        update_inspector();
    }

    void refresh() {
        m_refresh_requested = true;
    }

    void set_model_import_error_callback(std::function<void(const std::filesystem::path&, const std::string&)> callback) {
        m_model_import_error = std::move(callback);
    }

    // Returns true if the asset was dropped on a valid target in the components view.
    bool drop_asset(const std::filesystem::path& path, const bgui::vec2i& position) {
        if (!*m_surface_alive)
            return false;
        for (const auto& target : m_asset_targets) {
            bool enabled = true;
            for (auto* element = target.bounds; element; element = element->get_parent())
                enabled = enabled && element->is_enabled();
            if (!enabled)
                continue;
            const auto rect = target.bounds->processed_rect();
            if (position.x >= rect.x && position.x < rect.x + rect.z &&
                position.y >= rect.y && position.y < rect.y + rect.w) {
                target.apply(path);
                return true;
            }
        }
        return false;
    }

private:
    void record_edit(std::function<void()> undo, std::function<void()> redo) override {
        m_context->record_action(std::move(undo), std::move(redo));
    }

    using entity_id = editor_context::entity_id;
    std::function<void(const std::filesystem::path&, const std::string&)> m_model_import_error;

    std::shared_ptr<editor_context> m_context;
    bgui::window* m_window{nullptr};
    detail::refreshing_layout* m_root{nullptr};
    bgui::linear* m_toolbar{nullptr};
    bgui::linear* m_component_options{nullptr};
    bgui::tree* m_entity_tree{nullptr};
    std::optional<entity_id> m_rendered_selection;
    bool m_refresh_requested{false};
    commons::component::mask m_rendered_mask{commons::component::COMPONENTE_NONE};
    std::size_t m_history_revision{0};
    struct property_input {
        bgui::inputbox* input;
        std::function<std::string()> read_value;
    };
    std::vector<property_input> m_property_inputs;
    bgui::text* m_edit_status{nullptr};
    struct asset_target {
        bgui::element* bounds;
        std::function<void(const std::filesystem::path&)> apply;
    };
    std::vector<asset_target> m_asset_targets;
    std::shared_ptr<bool> m_surface_alive = std::make_shared<bool>(true);

    void track_property_input(
        bgui::inputbox& input,
        std::function<std::string()> read_value
    ) override {
        m_property_inputs.push_back({&input, std::move(read_value)});
    }

    void update_inspector() {
        refresh_if_needed();
        for (const auto& binding : m_property_inputs) {
            if (binding.input->is_focused())
                continue;
            const auto value = binding.read_value();
            if (binding.input->get_buffer() != value)
                binding.input->set_buffer(value);
        }
    }

    void remember_section_expansion() {
        for (auto& [layer, elements] : m_root->get_elements()) {
            (void)layer;
            for (const auto& element : elements) {
                auto* section = dynamic_cast<bgui::tree*>(element.get());
                if (!section)
                    continue;
                m_context->inspector_sections[section->label().get_buffer()] =
                    section->is_expanded();
            }
        }
    }

    void clear_content() {
        m_property_inputs.clear();
        m_asset_targets.clear();
        m_entity_tree = nullptr;
        for (auto& [layer, elements] : m_root->get_elements()) {
            (void)layer;
            while (elements.size() > 1)
                m_root->remove(elements.back().get());
        }
    }

    void refresh_if_needed() {
        if (!m_root || !*m_surface_alive)
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

        const auto mask = selected ? m_context->registry->get_components(*selected)
            : commons::component::COMPONENTE_NONE;
        const auto history_revision = m_context ? m_context->history_revision : 0;
        if (!m_refresh_requested && selected == m_rendered_selection &&
            mask == m_rendered_mask && history_revision == m_history_revision)
            return;

        m_refresh_requested = false;
        remember_section_expansion();
        m_rendered_selection = selected;
        m_rendered_mask = mask;
        m_history_revision = history_revision;
        clear_content();
        clear_toolbar();

        if (!selected && m_context->editor_camera_entity) {
            auto camera = m_context->registry->get<commons::camera>(*m_context->editor_camera_entity);
            if (camera) build_entity(*m_context->editor_camera_entity);
            return;
        }
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
        if (m_context->editor_camera_entity == id) {
            m_toolbar->add<bgui::text>("Editor Camera", 0.3f);
            if (auto camera = registry->get<commons::camera>(id))
                add_camera(id, camera);
            return;
        }
        build_toolbar(id);
        if (auto transform = registry->get<commons::transform>(id))
            add_transform(transform);

        if (auto renderer = registry->get<commons::renderer>(id))
            add_renderer(id, renderer);

        if (auto camera = registry->get<commons::camera>(id))
            add_camera(id, camera);

        if (auto light = registry->get<commons::directional_light>(id))
            add_directional_light(id, light);

        if (auto light = registry->get<commons::spot_light>(id))
            add_spot_light(id, light);

        if (auto listener = registry->get<commons::sound_listener>(id))
            add_sound_listener(id, listener);

        if (auto player = registry->get<commons::sound_player>(id))
            add_sound_player(id, player);

        if (auto physics = registry->get<commons::physics>(id)) {
            auto& section = add_section("Physics", true,
                [this, id]() { remove_component<commons::physics>(id); });
            auto& content = section.children();
            content.add<bgui::text>("Body Type", 0.27f);
            auto& types = content.add<bgui::combo_box>(
                std::vector<std::string>{"Rigid Body", "Character Body", "Static Body"},
                0.29f, static_cast<std::size_t>(physics->type));
            types.set_on_change([this, physics, &types](std::size_t index, const std::string&) {
                const auto before = physics->type;
                if (before == static_cast<commons::physics::body_type>(index))
                    return;
                const auto after = static_cast<commons::physics::body_type>(index);
                if (physics->e_forma == commons::physics::shapes::mesh_shape &&
                    physics->model_collider == commons::physics::model_collider_type::concave &&
                    after != commons::physics::body_type::static_body) {
                    if (m_edit_status)
                        m_edit_status->set_buffer("Concave model colliders require a Static Body.");
                    types.set_selected_index(static_cast<std::size_t>(before));
                    return;
                }
                const auto mass = physics->m_massa;
                physics->set_body_type(after);
                m_context->record_action([physics, before, mass]() {
                    physics->setMass(mass); physics->set_body_type(before);
                }, [physics, after]() { physics->set_body_type(after); });
            });
            content.add<bgui::text>("Collision Shape", 0.27f);
            const auto visual = registry->get<commons::renderer>(id);
            std::vector<std::string> shape_options{
                "Box", "Capsule", "Sphere", "Cylinder", "Cone", "Plane"
            };
            if ((visual && visual->m_modelo) ||
                physics->e_forma == commons::physics::shapes::mesh_shape)
                shape_options.push_back("Model");
            auto& shapes = content.add<bgui::combo_box>(
                std::move(shape_options),
                0.29f, static_cast<std::size_t>(physics->e_forma));
            shapes.set_on_change([this, physics, visual](std::size_t index, const std::string&) {
                const auto before = physics->e_forma;
                const auto after = static_cast<commons::physics::shapes>(index);
                auto apply = [physics, visual](auto shape) {
                    if (shape == commons::physics::shapes::mesh_shape &&
                        visual && visual->m_modelo)
                        physics->setModelo(visual->m_modelo);
                    physics->configure_collision(shape,
                        physics->collision_size, physics->collision_offset,
                        physics->collision_enabled, physics->trigger);
                };
                apply(after);
                m_context->record_action([apply, before]() { apply(before); },
                    [apply, after]() { apply(after); });
            });
            if (physics->e_forma == commons::physics::shapes::mesh_shape) {
                content.add<bgui::text>("Model Collider", 0.27f);
                auto& model_colliders = content.add<bgui::combo_box>(
                    std::vector<std::string>{"Convex Hull", "Concave Mesh"},
                    0.29f,
                    static_cast<std::size_t>(physics->model_collider)
                );
                model_colliders.set_on_change(
                    [this, physics, &model_colliders](
                        std::size_t index, const std::string&
                    ) {
                        const auto before = physics->model_collider;
                        const auto after = static_cast<
                            commons::physics::model_collider_type
                        >(index);
                        if (before == after)
                            return;
                        try {
                            physics->set_model_collider_type(after);
                        } catch (const std::invalid_argument&) {
                            if (m_edit_status)
                                m_edit_status->set_buffer(
                                    "Concave model colliders require a Static Body."
                                );
                            model_colliders.set_selected_index(
                                static_cast<std::size_t>(before)
                            );
                            return;
                        }
                        m_context->record_action(
                            [physics, before]() {
                                physics->set_model_collider_type(before);
                            },
                            [physics, after]() {
                                physics->set_model_collider_type(after);
                            }
                        );
                    }
                );
                if (physics->model_collider ==
                        commons::physics::model_collider_type::concave)
                    content.add<bgui::text>(
                        "Concave mesh colliders require a Static Body.", 0.25f
                    );
            }
            auto toggle = [this, physics, &content](const char* label, bool commons::physics::*field) {
                auto& checkbox = panel::add_checkbox(content, label, 0.29f, physics.get()->*field);
                checkbox.set_on_change([this, physics, field](bool value) {
                    const bool previous = physics.get()->*field;
                    physics.get()->*field = value;
                    m_context->record_action([physics, field, previous]() { physics.get()->*field = previous; },
                        [physics, field, value]() { physics.get()->*field = value; });
                });
            };
            toggle("Fit to Model", &commons::physics::fit_to_model);
            toggle("Collision Enabled", &commons::physics::collision_enabled);
            toggle("Trigger", &commons::physics::trigger);
            add_vec_field(content, "Rotation Offset", physics->collision_rotation,
                [physics]() { return physics->collision_rotation; },
                [physics](std::size_t axis, float value) { physics->collision_rotation[axis] = value; });
            for (bool offset : {false, true}) {
                auto get = [physics, offset]() { return offset ? physics->collision_offset : physics->collision_size; };
                add_vec_field(content, offset ? "Collider Offset" : "Collider Scale", get(), get,
                    [physics, offset](std::size_t axis, float value) {
                        auto size = physics->collision_size, position = physics->collision_offset;
                        (offset ? position : size)[axis] = offset ? value : std::max(0.001f, value);
                        physics->configure_collision(physics->e_forma, size, position,
                            physics->collision_enabled, physics->trigger);
                    });
            }
            add_float(content, "Friction", physics->friction,
                [physics]() { return physics->friction; },
                [physics](float value) { physics->setFriccao(std::max(0.f, value)); });
            add_float(content, "Restitution", physics->restitution,
                [physics]() { return physics->restitution; },
                [physics](float value) { physics->setRestituicao(std::clamp(value, 0.f, 1.f)); });
            add_float(section.children(), "Mass", physics->m_massa,
                [physics]() { return physics->m_massa; },
                [this, physics](float mass) {
                    if (physics->e_forma == commons::physics::shapes::mesh_shape &&
                        physics->model_collider ==
                            commons::physics::model_collider_type::concave &&
                        mass > 0.f) {
                        if (m_edit_status)
                            m_edit_status->set_buffer(
                                "Concave model colliders require a Static Body."
                            );
                        return;
                    }
                    physics->setMass(std::max(0.f, mass));
                });
        }
        add_script(id, registry->get<commons::code>(id));
        build_add_component(id);
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
        m_component_options = nullptr;
        m_edit_status = nullptr;
        for (auto& [layer, elements] : m_toolbar->get_elements()) {
            (void)layer;
            while (!elements.empty())
                m_toolbar->remove(elements.back().get());
        }
    }

    void build_toolbar(const entity_id id) {
        const auto registry = m_context->registry;
        auto& header = m_toolbar->add<bgui::linear>(
            bgui::orientation::horizontal
        );
        header.add_class("entities-toolbar");
        configure_row(header);

        auto& name_input = panel::add_input(header,
            entity_name(id),
            "Entity name...",
            0.3f,
            [this, id](const std::string& name) {
                const auto previous = m_context->entity_names.find(id);
                const std::optional<std::string> old_name =
                    previous == m_context->entity_names.end()
                    ? std::nullopt
                    : std::optional<std::string>{previous->second};
                const std::optional<std::string> new_name = name.empty()
                    ? std::nullopt
                    : std::optional<std::string>{name};
                if (old_name == new_name)
                    return;

                auto* names = &m_context->entity_names;
                auto apply_name = [names, id](const auto& value) {
                    if (value)
                        (*names)[id] = *value;
                    else
                        names->erase(id);
                };
                apply_name(new_name);
                m_context->record_action(
                    [apply_name, old_name]() { apply_name(old_name); },
                    [apply_name, new_name]() { apply_name(new_name); }
                );
                refresh();
            }
        );
        name_input.add_class("editor-input");
        name_input.style.layout.require_mode(
            bgui::mode::stretch,
            bgui::mode::wrap_content
        );

    }

    void build_add_component(const entity_id id) {
        const auto registry = m_context->registry;
        auto& add_component_button = m_root->add<bgui::button>(
            "Add Component",
            0.28f,
            [this]() {
                if (m_component_options)
                    m_component_options->set_enable(
                        !m_component_options->is_enabled()
                    );
            }
        );
        add_component_button.add_class("editor-input");
        add_component_button.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        add_component_button.style.layout.align = {bgui::alignment::center, bgui::alignment::center};

        const bool can_add_camera = !registry->get<commons::camera>(id);
        const bool can_add_directional_light =
            !registry->get<commons::directional_light>(id);
        const bool can_add_spot_light = !registry->get<commons::spot_light>(id);
        const bool can_add_sound_listener = !registry->get<commons::sound_listener>(id);
        const bool can_add_sound_player = !registry->get<commons::sound_player>(id);
        m_component_options = &m_root->add<bgui::linear>(
            bgui::orientation::vertical
        );
        m_component_options->add_class("editor-surface");
        m_component_options->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_component_options->set_enable(false);
        if (!registry->get<commons::transform>(id))
            m_component_options->add<bgui::button>("Transform", 0.28f,
                [this, id]() { add_component<commons::transform>(id); });
        if (!registry->get<commons::renderer>(id))
            m_component_options->add<bgui::button>("Renderer", 0.28f,
                [this, id]() { add_component<commons::renderer>(id); });
        if (can_add_camera) {
            auto& camera_button = m_component_options->add<bgui::button>(
                "Camera",
                0.28f,
                [this, id]() { add_component<commons::camera>(id); }
            );
            camera_button.add_class("editor-input");
        }
        if (can_add_directional_light) {
            auto& light_button = m_component_options->add<bgui::button>(
                "Directional Light",
                0.28f,
                [this, id]() {
                    add_component<commons::directional_light>(id);
                }
            );
            light_button.add_class("editor-input");
        }
        if (can_add_spot_light) {
            auto& light_button = m_component_options->add<bgui::button>(
                "Spot Light",
                0.28f,
                [this, id]() { add_component<commons::spot_light>(id); }
            );
            light_button.add_class("editor-input");
        }
        if (can_add_sound_listener)
            m_component_options->add<bgui::button>("Sound Listener", 0.28f,
                [this, id]() { add_component<commons::sound_listener>(id); });
        if (can_add_sound_player)
            m_component_options->add<bgui::button>("Sound Player", 0.28f,
                [this, id]() { add_component<commons::sound_player>(id); });
        if (!registry->get<commons::physics>(id))
            m_component_options->add<bgui::button>("Physics", 0.28f,
                [this, id]() { add_component<commons::physics>(id); });
        auto& script_input = panel::add_input(*m_component_options,
            "", "Add Lua script: drop a file or enter its path", 0.27f);
        configure_property_input(script_input);
        script_input.style.layout.require_width(bgui::mode::match_parent);
        auto append_script = [this, id](const std::filesystem::path& path) {
            std::string error;
            if (attach_lua_script(m_context, id, path, error)) refresh();
            else m_edit_status->set_buffer(error);
        };
        script_input.m_enter_func = [append_script](const std::string& path) { append_script(path); };
        m_asset_targets.push_back({&script_input, append_script});
        m_edit_status = &m_root->add<bgui::text>("", 0.25f);
    }

    void add_rgb_picker(bgui::linear& content, const std::string& label,
        std::function<bgui::vec3()> read, std::function<void(bgui::vec3)> write) {
        content.add<bgui::text>(label, .27f);
        const auto value = read();
        auto& picker = panel::add_color(content, {value.x, value.y, value.z, 1.f});
        if (label == "Fog") picker.add_class("inspector-fog-color");
        picker.set_on_change([this, read, write](const bgui::color& c) {
            const auto before = read();
            const bgui::vec3 after{c.x,c.y,c.z};
            if (before == after) return;
            write(after);
            m_context->record_action([write,before] { write(before); }, [write,after] { write(after); });
        });
    }

    bgui::tree& add_section(
        const std::string& name,
        const bool removable,
        const std::function<void()>& remove_component
    ) {
        const auto expansion = m_context->inspector_sections.find(name);
        const bool expanded = expansion == m_context->inspector_sections.end()
            ? true
            : expansion->second;
        auto& section = m_root->add<bgui::tree>(name, 0.28f, expanded);
        section.add_class("editor-section");
        section.set_icon(name == "Transform" ? "directory_open" :
            name == "Renderer" ? "cube" : name == "Camera" ? "camera" :
            name == "Directional Light" || name == "Spot Light" ? "lightbulb" :
            name == "Sound Listener" || name == "Sound Player" ? "circle" :
            name == "Physics" ? "circle" : "code", 0.24f);
        section.children().add_class("editor-surface");
        section.children().style.layout.padding = bgui::vec4i{6, 4, 6, 4};
        section.children().style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::start,
            bgui::alignment::start
        };

        if (removable) {
            auto& remove_button = section.header().add<bgui::button>(
                "",
                0.27f,
                remove_component
            );
            remove_button.add_class("editor-icon-button");
            remove_button.add_class("component-remove-button");
            remove_button.style.layout.require_size(22.f, 22.f);
            remove_button.get_label().set_enable(false);
            remove_button.add<bgui::icon>("trash_alt", 0.25f).receives_input(false);
        }

        return section;
    }

    template<typename Component>
    void remove_component(const entity_id id) {
        if (!m_context || !m_context->registry)
            return;
        auto registry = m_context->registry;
        auto component = registry->get<Component>(id);
        if (!component)
            return;
        registry->remove<Component>(id);
        m_context->record_action(
            [registry, id, component]() {
                registry->entities[id][Component::mask] = component;
                registry->mascaras[id] |= Component::mask;
            },
            [registry, id]() { registry->remove<Component>(id); }
        );
        refresh();
    }

    void add_transform(const std::shared_ptr<commons::transform>& transform) {
        auto& section = add_section("Transform", false, {});
        auto& content = section.children();

        add_vec_field(
            content,
            "Position",
            transform->get_position(),
            [transform]() { return transform->get_position(); },
            [transform](std::size_t axis, float value) {
                auto position = transform->get_position();
                position[axis] = value;
                transform->set_position(position);
            }
        );
        add_vec_field(
            content,
            "Rotation",
            transform->get_rotation(),
            [transform]() { return transform->get_rotation(); },
            [transform](std::size_t axis, float value) {
                auto rotation = transform->get_rotation();
                rotation[axis] = value;
                transform->set_rotation(rotation);
            }
        );
        add_vec_field(
            content,
            "Scale",
            transform->get_scale(),
            [transform]() { return transform->get_scale(); },
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
        add_toggle(section.children(), "Cast Shadows", renderer, &commons::renderer::cast_shadows);
        add_toggle(section.children(), "Receive Shadows", renderer, &commons::renderer::receive_shadows);
        auto& model_field = panel::add_field(section.children(),
            "Model", renderer->m_modelo ? renderer->m_modelo->get_source_path() : "", 0.27f);
        configure_property_field(model_field);
        auto* model_input = &model_field.get_inputbox();
        auto assign_model = [this, id, renderer](const std::filesystem::path& requested) {
            auto path = requested.is_absolute() ? requested : m_context->project_root / requested;
            try {
                if (commons::supports_model_import(path) && std::filesystem::is_regular_file(path))
                    (void)asset_dependencies(std::filesystem::canonical(path), true);
                auto model = std::make_unique<commons::model>(path.string());
                if (model->meshes.empty()) {
                    m_edit_status->set_buffer("Could not load model: " + requested.string());
                    if (m_model_import_error) m_model_import_error(path, "Não foi possível carregar o modelo.");
                    return;
                }
                auto registry = m_context->registry;
                auto next = std::make_shared<commons::renderer>(model.release());
                next->my_object = id;
                next->reg = registry;
                next->visible = renderer->visible;
                next->cast_shadows = renderer->cast_shadows;
                next->receive_shadows = renderer->receive_shadows;
                auto apply = [registry, id](const auto& component) {
                    registry->entities[id][commons::renderer::mask] = component;
                };
                apply(next);
                m_context->record_action(
                    [apply, renderer]() { apply(renderer); },
                    [apply, next]() { apply(next); });
                refresh();
            } catch (const std::exception& error) {
                m_edit_status->set_buffer(error.what());
                if (m_model_import_error) m_model_import_error(path, error.what());
            }
        };
        model_input->m_enter_func = [assign_model](const std::string& path) { assign_model(path); };
        m_asset_targets.push_back({model_input, assign_model});

        if (!renderer->m_modelo)
            return;
        for (std::size_t index = 0; index < renderer->m_modelo->meshes.size(); ++index) {
            auto& group = section.children().add<bgui::tree>("Malha " + std::to_string(index + 1), .28f, true);
            build_mesh_material_editor(*this, group.children(), m_context, renderer, index,
                [this](auto* element, auto assign) { m_asset_targets.push_back({element, std::move(assign)}); },
                [this](std::string message) { if (m_edit_status) m_edit_status->set_buffer(std::move(message)); });
        }
    }

    void add_script(entity_id id, const std::shared_ptr<commons::code>& script) {
        if (!script) return;
        auto entries = script->scripts;
        entries.insert(entries.begin(), script);
        for (std::size_t index = 0; index < entries.size(); ++index) {
            auto& section = add_section("Lua Script " + std::to_string(index + 1), true,
                [this, id, script, index]() {
                    auto registry = m_context->registry;
                    auto before = script->scripts;
                    if (index == 0) {
                        std::shared_ptr<commons::code> next;
                        if (!before.empty()) {
                            next = std::make_shared<commons::code>(before.front()->file);
                            next->arquivoCompleto = before.front()->arquivoCompleto;
                            next->properties = before.front()->properties;
                            next->asset_root = before.front()->asset_root;
                            next->reg = registry;
                            next->my_object = id;
                            next->scripts.assign(before.begin() + 1, before.end());
                        }
                        auto apply = [registry, id](auto value) {
                            if (value) registry->entities[id][commons::code::mask] = value;
                            else registry->remove<commons::code>(id);
                            if (value) registry->mascaras[id] |= commons::code::mask;
                        };
                        apply(next);
                        m_context->record_action([apply, script]() { apply(script); },
                            [apply, next]() { apply(next); });
                    } else {
                        auto after = before;
                        after.erase(after.begin() + index - 1);
                        script->scripts = after;
                        m_context->record_action([script, before]() { script->scripts = before; },
                            [script, after]() { script->scripts = after; });
                    }
                    refresh();
                });
            auto& input = panel::add_input(section.children(),
                entries[index]->file, "Drop a Lua script or enter its path", 0.27f);
            configure_property_input(input);
            input.style.layout.require_width(bgui::mode::match_parent);
            auto assign = [this, id, index](const std::filesystem::path& path) {
                std::string error;
                if (attach_lua_script(m_context, id, path, error, index)) refresh();
                else m_edit_status->set_buffer(error);
            };
            input.m_enter_func = [assign](const std::string& path) { assign(path); };
            m_asset_targets.push_back({&input, assign});
            auto entry = entries[index];
            std::string error;
            if (!entry->load_properties(error)) {
                section.children().add<bgui::text>(error, 0.27f);
                continue;
            }
            for (const auto& property : entry->properties)
                add_script_property(section.children(), id, entry, property);
        }
    }

    static bool is_script_image(const std::filesystem::path& path) {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
            extension == ".bmp" || extension == ".tga";
    }

    void add_script_property(bgui::linear& content, entity_id id,
        const std::shared_ptr<commons::code>& script, const commons::script_property& property) {
        using value_type = commons::script_property::value_type;
        auto change = [this, id, script, name = property.name](const value_type& next) {
            if (m_context->is_playing || m_context->locked_entities.contains(id)) return;
            const auto it = std::find_if(script->properties.begin(), script->properties.end(),
                [&](const auto& p) { return p.name == name; });
            if (it == script->properties.end() || it->value == next) return;
            const auto before = it->value;
            if (!script->set_property(name, next)) return;
            m_context->record_action([script, name, before]() { script->set_property(name, before); },
                [script, name, next]() { script->set_property(name, next); });
        };
        if (property.type == "boolean") {
            auto& checkbox = panel::add_checkbox(content, property.name, .29f, std::get<bool>(property.value));
            checkbox.set_on_change([change](bool value) { change(value); });
        } else if (property.type == "color") {
            content.add<bgui::text>(property.name, .27f);
            const auto color = std::get<std::array<float, 4>>(property.value);
            auto& picker = panel::add_color(content, bgui::color{color[0], color[1], color[2], color[3]});
            picker.set_on_change([change](const bgui::color& value) {
                change(std::array<float, 4>{value.x, value.y, value.z, value.w});
            });
            auto& alpha = panel::add_field(content, property.name + " Alpha", std::to_string(color[3]), .27f,
                [this, script, name = property.name, change](const std::string& text) {
                    float value{};
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                        !std::isfinite(value) || value < 0 || value > 1) {
                        m_edit_status->set_buffer("Alpha must be between 0 and 1."); return;
                    }
                    for (const auto& current : script->properties) {
                        if (current.name != name || current.type != "color") continue;
                        auto next = std::get<std::array<float, 4>>(current.value);
                        next[3] = value;
                        change(next);
                        break;
                    }
                });
            configure_property_field(alpha);
        } else {
            const auto buffer = property.type == "number" ? std::to_string(std::get<double>(property.value)) :
                std::get<std::string>(property.value);
            auto assign = [this, change, type = property.type](const std::string& text) {
                if (type == "number") {
                    double value{};
                    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || !std::isfinite(value)) {
                        m_edit_status->set_buffer("Invalid number."); return;
                    }
                    change(value);
                } else if (type == "image" && !text.empty()) {
                    const auto path = std::filesystem::absolute(std::filesystem::path(text).is_absolute()
                        ? std::filesystem::path(text) : m_context->project_root / text).lexically_normal();
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(path, error) || !is_script_image(path)) {
                        m_edit_status->set_buffer("Choose an image file."); return;
                    }
                    const auto relative = path.lexically_relative(m_context->project_root);
                    change(!relative.empty() && *relative.begin() != ".." ? relative.generic_string() : path.string());
                } else change(text);
            };
            auto& field = panel::add_field(content, property.name, buffer, .27f, assign);
            configure_property_field(field);
            field.add_class("lua-property-" + property.type);
            if (property.type == "image") m_asset_targets.push_back({&field.get_inputbox(),
                [assign](const std::filesystem::path& path) { assign(path.string()); }});
        }
    }

    void add_camera(
        const entity_id id,
        const std::shared_ptr<commons::camera>& camera
    ) {
        auto& section = add_section(
            "Camera",
            m_context->editor_camera_entity != id,
            [this, id]() { remove_component<commons::camera>(id); }
        );
        auto& content = section.children();

        add_float(
            content,
            "Field of View",
            camera->config.fov,
            [camera]() { return camera->config.fov; },
            [camera](float value) { camera->config.fov = std::clamp(value, 1.f, 179.f); }
        );
        add_float(
            content,
            "Near Clip",
            camera->config.corte_curto,
            [camera]() { return camera->config.corte_curto; },
            [camera](float value) {
                if (value > 0.f && value < camera->config.corte_longo)
                    camera->config.corte_curto = value;
            }
        );
        add_float(
            content,
            "Far Clip",
            camera->config.corte_longo,
            [camera]() { return camera->config.corte_longo; },
            [camera](float value) {
                if (value > camera->config.corte_curto) camera->config.corte_longo = value;
            }
        );
        add_float(
            content,
            "Orthographic Scale",
            camera->config.scale,
            [camera]() { return camera->config.scale; },
            [camera](float value) { if (value > 0.f) camera->config.scale = value; }
        );

        content.add<bgui::text>("With skybox disabled, the atmosphere gradient is used.", 0.25f);
        auto& skybox_enabled = panel::add_checkbox(content,
            "Enable Skybox", 0.29f, camera->config.use_skybox);
        skybox_enabled.set_on_change([this, camera](bool enabled) {
            const bool previous = camera->config.use_skybox;
            if (previous == enabled) return;
            camera->config.use_skybox = enabled;
            m_context->record_action(
                [camera, previous]() { camera->config.use_skybox = previous; },
                [camera, enabled]() { camera->config.use_skybox = enabled; });
        });
        auto& row = content.add<bgui::linear>(bgui::orientation::horizontal);
        configure_property_row(row);
        auto& label = row.add<bgui::text>("Cubemap image (4 x 3 cross)", 0.27f);
        configure_property_label(label);
        auto& input = panel::add_input(row, camera->config.skybox_image, "Drop a horizontal cross image", 0.26f);
        configure_property_input(input);
        auto assign = [this, camera](const std::filesystem::path& path) {
            auto before = camera->config.skybox_image;
            auto after = path.generic_string();
            if (path.is_absolute() && !m_context->project_root.empty()) {
                const auto relative = path.lexically_relative(m_context->project_root);
                if (!relative.empty() && *relative.begin() != "..") after = relative.generic_string();
            }
            camera->skybox_asset_root = m_context->project_root.string();
            camera->config.skybox_image = after;
            m_context->record_action(
                [camera, before]() { camera->config.skybox_image = before; },
                [camera, after]() { camera->config.skybox_image = after; });
        };
        input.m_enter_func = [assign](const std::string& path) { assign(path); };
        m_asset_targets.push_back({&input, assign});
        if (camera->config.skybox_image.empty() && std::any_of(camera->skybox_faces.begin(), camera->skybox_faces.end(),
                [](const auto& path) { return !path.empty(); }))
            content.add<bgui::text>("Legacy six-face cubemap loaded; select a cross image to replace it.", 0.25f);
        add_rgb_picker(content, "Atmosphere Zenith", [camera] { return camera->config.atmosphere_zenith; },
            [camera](bgui::vec3 value) { camera->config.atmosphere_zenith = value; });
        add_rgb_picker(content, "Atmosphere Horizon", [camera] {
            auto c = camera->config.sky_solid_color; return bgui::vec3{c.r,c.g,c.b}; },
            [camera](bgui::vec3 v) { camera->config.sky_solid_color = {v.x,v.y,v.z,1.f}; });

        auto& fog_enabled = panel::add_checkbox(content,
            "Enable Fog", 0.29f, camera->config.fog_enabled);
        fog_enabled.set_on_change([this, camera](bool enabled) {
            const bool previous = camera->config.fog_enabled;
            if (previous == enabled) return;
            camera->config.fog_enabled = enabled;
            m_context->record_action(
                [camera, previous]() { camera->config.fog_enabled = previous; },
                [camera, enabled]() { camera->config.fog_enabled = enabled; });
        });
        add_float(content, "Fog Density", camera->config.fog_density,
            [camera]() { return camera->config.fog_density; },
            [camera](float value) {
                if (std::isfinite(value) && value >= 0.f)
                    camera->config.fog_density = value;
            });
        add_rgb_picker(content, "Fog", [camera] {
            auto c = camera->config.fog_color; return bgui::vec3{c.r,c.g,c.b}; },
            [camera](bgui::vec3 v) { camera->config.fog_color = {v.x,v.y,v.z,1.f}; });

        auto& orthographic = panel::add_checkbox(content,
            "Orthographic",
            0.29f,
            camera->config.flag_orth
        );
        orthographic.set_on_change(
            [this, camera](bool enabled) {
                const bool previous = camera->config.flag_orth;
                if (previous == enabled)
                    return;
                camera->config.flag_orth = enabled;
                m_context->record_action(
                    [camera, previous]() { camera->config.flag_orth = previous; },
                    [camera, enabled]() { camera->config.flag_orth = enabled; }
                );
            }
        );

    }

    template<class Content, class Component>
    void add_toggle(Content& content, const char* label,
        std::shared_ptr<Component> component, bool Component::*field) {
        auto& checkbox = panel::add_checkbox(content, label, 0.29f, component.get()->*field);
        checkbox.set_on_change([this, component, field](bool enabled) {
            const bool previous = component.get()->*field;
            if (previous == enabled) return;
            component.get()->*field = enabled;
            m_context->record_action(
                [component, field, previous]() { component.get()->*field = previous; },
                [component, field, enabled]() { component.get()->*field = enabled; });
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

        add_toggle(content, "Shadows", light, &commons::directional_light::shadows);
        add_float(content, "Shadow Resolution", static_cast<float>(light->shadow_resolution),
            [light]() { return static_cast<float>(light->shadow_resolution); },
            [light](float v) { light->shadow_resolution = static_cast<int>(std::clamp(v, 128.f, 8192.f)); });
        add_float(content, "Shadow Distance", light->shadow_distance,
            [light]() { return light->shadow_distance; }, [light](float v) { light->shadow_distance = v; });
        add_float(content, "Shadow Bias", light->shadow_bias,
            [light]() { return light->shadow_bias; }, [light](float v) { light->shadow_bias = v; });
        add_float(content, "Shadow Strength", light->shadow_strength,
            [light]() { return light->shadow_strength; }, [light](float v) { light->shadow_strength = v; });
        add_vec_field(
            content,
            "Direction",
            light->direction,
            [light]() { return light->direction; },
            [light](std::size_t axis, float value) {
                light->direction[axis] = value;
            }
        );
        add_rgb_picker(content, "Ambient", [light] { return light->ambient; },
            [light](bgui::vec3 v) { light->ambient = v; });
        add_rgb_picker(content, "Color", [light] { return light->color; },
            [light](bgui::vec3 v) { light->color = v; });
        add_float(
            content,
            "Intensity",
            light->intensity,
            [light]() { return light->intensity; },
            [light](float value) { light->intensity = value; }
        );
    }

    void add_spot_light(
        const entity_id id,
        const std::shared_ptr<commons::spot_light>& light
    ) {
        auto& section = add_section(
            "Spot Light",
            true,
            [this, id]() { remove_component<commons::spot_light>(id); }
        );
        auto& content = section.children();
        add_toggle(content, "Shadows", light, &commons::spot_light::shadows);
        add_float(content, "Shadow Distance", light->shadow_distance,
            [light]() { return light->shadow_distance; },
            [light](float v) { light->shadow_distance = std::clamp(v, 1.f, 10000.f); });
        add_float(content, "Shadow Bias", light->shadow_bias,
            [light]() { return light->shadow_bias; },
            [light](float v) { light->shadow_bias = std::clamp(v, 0.f, 0.1f); });
        add_float(content, "Shadow Strength", light->shadow_strength,
            [light]() { return light->shadow_strength; },
            [light](float v) { light->shadow_strength = std::clamp(v, 0.f, 1.f); });
        add_vec_field(content, "Position", light->position,
            [light]() { return light->position; },
            [light](std::size_t axis, float value) { light->position[axis] = value; });
        add_vec_field(content, "Direction", light->direction,
            [light]() { return light->direction; },
            [light](std::size_t axis, float value) { light->direction[axis] = value; });
        content.add<bgui::text>("Color", 0.27f);
        auto& color_picker = panel::add_color(content,
            bgui::color{light->color.x, light->color.y, light->color.z, 1.f});
        color_picker.set_on_change([this, light](const bgui::color& color) {
            const auto previous = light->color;
            const bgui::vec3 next{color.x, color.y, color.z};
            if (previous == next)
                return;
            light->color = next;
            m_context->record_action(
                [light, previous]() { light->color = previous; },
                [light, next]() { light->color = next; });
        });
        add_float(content, "Intensity", light->intensity,
            [light]() { return light->intensity; },
            [light](float value) { light->intensity = value; });
        add_float(content, "Constant Attenuation", light->constant,
            [light]() { return light->constant; },
            [light](float value) { light->constant = value; });
        add_float(content, "Linear Attenuation", light->linear,
            [light]() { return light->linear; },
            [light](float value) { light->linear = value; });
        add_float(content, "Quadratic Attenuation", light->quadratic,
            [light]() { return light->quadratic; },
            [light](float value) { light->quadratic = value; });
        add_float(content, "Inner Cutoff (degrees)", light->inner_cutoff,
            [light]() { return light->inner_cutoff; },
            [light](float value) { light->set_inner_cutoff(value); });
        add_float(content, "Outer Cutoff (degrees)", light->outer_cutoff,
            [light]() { return light->outer_cutoff; },
            [light](float value) { light->set_outer_cutoff(value); });
    }

    void add_sound_listener(
        const entity_id id,
        const std::shared_ptr<commons::sound_listener>& listener
    ) {
        auto& section = add_section(
            "Sound Listener",
            true,
            [this, id]() { remove_component<commons::sound_listener>(id); }
        );
        add_toggle(section.children(), "Enabled", listener, &commons::sound_listener::enabled);
    }

    void add_sound_player(
        const entity_id id,
        const std::shared_ptr<commons::sound_player>& player
    ) {
        auto& section = add_section(
            "Sound Player",
            true,
            [this, id]() { remove_component<commons::sound_player>(id); }
        );
        auto& content = section.children();
        auto& clip_field = panel::add_field(content, "Audio Clip", player->clip, 0.27f);
        configure_property_field(clip_field);
        auto& clip_input = clip_field.get_inputbox();
        const auto assign_clip = [this, player, input = &clip_input](
            const std::filesystem::path& requested
        ) {
            std::string next = requested.generic_string();
            if (requested.is_absolute() && !m_context->project_root.empty()) {
                const auto relative = requested.lexically_relative(m_context->project_root);
                if (!relative.empty() && *relative.begin() != "..")
                    next = relative.generic_string();
            }
            const auto previous = player->clip;
            if (previous == next)
                return;
            player->clip = next;
            input->set_buffer(next);
            m_context->record_action(
                [player, previous]() { player->clip = previous; },
                [player, next]() { player->clip = next; }
            );
        };
        clip_input.m_enter_func = [assign_clip](const std::string& path) {
            assign_clip(path);
        };
        track_property_input(clip_input, [player]() { return player->clip; });
        m_asset_targets.push_back({&clip_input, assign_clip});

        add_float(content, "Volume", player->volume,
            [player]() { return player->volume; },
            [player](float value) { player->volume = std::clamp(value, 0.f, 1.f); });
        add_float(content, "Pitch", player->pitch,
            [player]() { return player->pitch; },
            [player](float value) { player->pitch = std::clamp(value, 0.1f, 3.f); });
        add_float(content, "Area Radius", player->radius,
            [player]() { return player->radius; },
            [player](float value) { player->radius = std::clamp(value, 0.1f, 10000.f); });
        add_toggle(content, "Looping", player, &commons::sound_player::looping);
        add_toggle(content, "Play On Awake", player, &commons::sound_player::play_on_awake);
    }

    template<typename Component>
    void add_component(const entity_id id) {
        if (!m_context || !m_context->registry)
            return;
        auto entity = commons::entity{
            id,
            m_context->registry->get_components(id)
        };
        if constexpr (std::is_same_v<Component, commons::renderer>)
            m_context->registry->add<Component>(entity, new commons::model());
        else
            m_context->registry->add<Component>(entity);
        auto registry = m_context->registry;
        auto component = registry->get<Component>(id);
        m_context->record_action(
            [registry, id]() { registry->remove<Component>(id); },
            [registry, id, component]() {
                registry->entities[id][Component::mask] = component;
                registry->mascaras[id] |= Component::mask;
            }
        );
        refresh();
    }
};

}
