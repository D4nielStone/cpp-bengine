#pragma once
#include "../systems/asset_import.hpp"

#include "editor_context.hpp"
#include "panel.hpp"

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "util/pbr_material.hpp"
#include "script_tools.hpp"

#include <bgui.hpp>
#include <elem/checkbox.hpp>
#include <elem/combo_box.hpp>
#include <elem/color_picker.hpp>
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
#include <utility>

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
        m_root->before_update = [this]() { refresh_if_needed(); };
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

    void update() override {
        refresh_if_needed();
    }

    void refresh() {
        m_refresh_requested = true;
    }

    void set_model_import_error_callback(std::function<void(const std::filesystem::path&, const std::string&)> callback) {
        m_model_import_error = std::move(callback);
    }

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
    bgui::text* m_edit_status{nullptr};
    struct asset_target {
        bgui::element* bounds;
        std::function<void(const std::filesystem::path&)> apply;
    };
    std::vector<asset_target> m_asset_targets;
    std::shared_ptr<bool> m_surface_alive = std::make_shared<bool>(true);

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

    static void configure_property_label(bgui::text& label) {
        label.add_class("editor-property-label");
        label.style.layout.require_mode(bgui::mode::pixel, bgui::mode::wrap_content);
        label.style.layout.require_width(bgui::mode::pixel, 112.f);
        label.style.layout.margin = bgui::vec4i{0, 0, 4, 0};
    }

    static void configure_property_row(bgui::linear& row) {
        configure_row(row);
        row.style.layout.padding = bgui::vec4i{0, 0, 0, 0};
        row.style.layout.margin = bgui::vec4i{0, 2, 0, 2};
    }

    static void configure_property_input(bgui::inputbox& input) {
        input.add_class("editor-input");
        input.style.layout.require_mode(bgui::mode::stretch, bgui::mode::pixel);
        input.style.layout.require_height(bgui::mode::pixel, 22.f);
        input.style.layout.margin = bgui::vec4i{0, 0, 0, 0};
        input.style.layout.padding = bgui::vec4i{4, 2, 4, 2};
    }

    static void configure_property_field(bgui::field& field) {
        configure_property_row(field);
        configure_property_label(field.get_label());
        configure_property_input(field.get_inputbox());
    }

    void clear_content() {
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
        m_rendered_selection = selected;
        m_rendered_mask = mask;
        m_history_revision = history_revision;
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
        if (auto transform = registry->get<commons::transform>(id))
            add_transform(transform);

        if (auto renderer = registry->get<commons::renderer>(id))
            add_renderer(id, renderer);

        if (auto camera = registry->get<commons::camera>(id))
            add_camera(id, camera);

        if (auto light = registry->get<commons::directional_light>(id))
            add_directional_light(id, light);

        if (auto physics = registry->get<commons::physics>(id)) {
            auto& section = add_section("Physics", true,
                [this, id]() { remove_component<commons::physics>(id); });
            auto& content = section.children();
            content.add<bgui::text>("Body Type", 0.27f);
            auto& types = content.add<bgui::combo_box>(
                std::vector<std::string>{"Rigid Body", "Character Body", "Static Body"},
                0.29f, static_cast<std::size_t>(physics->type));
            types.set_on_change([this, physics](std::size_t index, const std::string&) {
                const auto before = physics->type;
                const auto mass = physics->m_massa;
                const auto after = static_cast<commons::physics::body_type>(index);
                physics->set_body_type(after);
                m_context->record_action([physics, before, mass]() {
                    physics->setMass(mass); physics->set_body_type(before);
                }, [physics, after]() { physics->set_body_type(after); });
            });
            content.add<bgui::text>("Collision Shape", 0.27f);
            auto& shapes = content.add<bgui::combo_box>(
                std::vector<std::string>{"Box", "Capsule", "Sphere", "Cylinder", "Cone", "Plane"},
                0.29f, static_cast<std::size_t>(physics->e_forma));
            shapes.set_on_change([this, physics](std::size_t index, const std::string&) {
                const auto before = physics->e_forma;
                const auto after = static_cast<commons::physics::shapes>(index);
                auto apply = [physics](auto shape) { physics->configure_collision(shape,
                    physics->collision_size, physics->collision_offset,
                    physics->collision_enabled, physics->trigger); };
                apply(after);
                m_context->record_action([apply, before]() { apply(before); },
                    [apply, after]() { apply(after); });
            });
            auto toggle = [this, physics, &content](const char* label, bool commons::physics::*field) {
                auto& checkbox = content.add<bgui::checkbox>(label, 0.29f, physics.get()->*field);
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
            add_vector_field(content, "Rotation Offset", physics->collision_rotation,
                [physics]() { return physics->collision_rotation; },
                [physics](std::size_t axis, float value) { physics->collision_rotation[axis] = value; });
            for (bool offset : {false, true}) {
                auto get = [physics, offset]() { return offset ? physics->collision_offset : physics->collision_size; };
                add_vector_field(content, offset ? "Collider Offset" : "Collider Scale", get(), get,
                    [physics, offset](std::size_t axis, float value) {
                        auto size = physics->collision_size, position = physics->collision_offset;
                        (offset ? position : size)[axis] = offset ? value : std::max(0.001f, value);
                        physics->configure_collision(physics->e_forma, size, position,
                            physics->collision_enabled, physics->trigger);
                    });
            }
            add_number_field(content, "Friction", physics->friction,
                [physics]() { return physics->friction; },
                [physics](float value) { physics->setFriccao(std::max(0.f, value)); });
            add_number_field(content, "Restitution", physics->restitution,
                [physics]() { return physics->restitution; },
                [physics](float value) { physics->setRestituicao(std::clamp(value, 0.f, 1.f)); });
            add_number_field(section.children(), "Mass", physics->m_massa,
                [physics]() { return physics->m_massa; },
                [physics](float mass) { physics->setMass(std::max(0.f, mass)); });
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

        auto& name_input = header.add<bgui::inputbox>(
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

        const bool can_add_camera = !registry->get<commons::camera>(id);
        const bool can_add_light =
            !registry->get<commons::directional_light>(id);
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
        if (can_add_light) {
            auto& light_button = m_component_options->add<bgui::button>(
                "Directional Light",
                0.28f,
                [this, id]() {
                    add_component<commons::directional_light>(id);
                }
            );
            light_button.add_class("editor-input");
        }
        if (!registry->get<commons::physics>(id))
            m_component_options->add<bgui::button>("Physics", 0.28f,
                [this, id]() { add_component<commons::physics>(id); });
        auto& script_input = m_component_options->add<bgui::inputbox>(
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

    bgui::tree& add_section(
        const std::string& name,
        const bool removable,
        const std::function<void()>& remove_component
    ) {
        auto& section = m_root->add<bgui::tree>(name, 0.28f, true);
        section.set_expanded(true);
        section.add_class("editor-section");
        section.set_icon(name == "Transform" ? "directory_open" :
            name == "Renderer" ? "cube" : name == "Camera" ? "camera" :
            name == "Directional Light" ? "lightbulb" : name == "Physics" ? "circle" : "code", 0.24f);
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

        add_vector_field(
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
        add_vector_field(
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
        add_vector_field(
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
        add_shadow_toggle(section.children(), "Cast Shadows", renderer, &commons::renderer::cast_shadows);
        add_shadow_toggle(section.children(), "Receive Shadows", renderer, &commons::renderer::receive_shadows);
        auto& model_field = section.children().add<bgui::field>(
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
        for (std::size_t index = 0;
             index < renderer->m_modelo->meshes.size();
             ++index) {
            auto& mesh = renderer->m_modelo->meshes[index];
            const auto material = mesh.getMaterial();
            const auto albedo = material.getUniforme("material.albedo");
            commons::color base_color;
            if (albedo.m_type == commons::uniform::type::Cor) {
                base_color = albedo.valor.color_value;
            } else if (albedo.m_type == commons::uniform::type::Cor_ptr &&
                       albedo.ponteiro.color_ptr) {
                base_color = *albedo.ponteiro.color_ptr;
            }

            auto& material_label = section.children().add<bgui::text>(
                "Material " + std::to_string(index + 1) + " Base Color",
                0.29f
            );
            material_label.add_class("editor-property-label");
            material_label.style.layout.require_mode(
                bgui::mode::match_parent,
                bgui::mode::wrap_content
            );

            auto& color_picker = section.children().add<bgui::color_picker>(
                bgui::color{
                    base_color.r,
                    base_color.g,
                    base_color.b,
                    base_color.a
                }
            );
            color_picker.add_class("editor-input");
            color_picker.style.layout.require_size(128.f, 92.f);
            color_picker.set_on_change(
                [this, renderer, index, base_color](const bgui::color& color) {
                    if (!renderer->m_modelo ||
                        index >= renderer->m_modelo->meshes.size())
                        return;
                    auto& mesh = renderer->m_modelo->meshes[index];
                    auto material = mesh.getMaterial();
                    const auto before = material.getUniforme("material.albedo");
                    const commons::color previous = before.m_type ==
                        commons::uniform::type::Cor
                        ? before.valor.color_value
                        : before.m_type == commons::uniform::type::Cor_ptr &&
                            before.ponteiro.color_ptr
                            ? *before.ponteiro.color_ptr
                            : base_color;
                    const commons::color next{
                        color.x, color.y, color.z, color.w
                    };
                    if (previous.r == next.r && previous.g == next.g &&
                        previous.b == next.b && previous.a == next.a)
                        return;

                    auto apply_color = [renderer, index](commons::color value) {
                        if (!renderer->m_modelo ||
                            index >= renderer->m_modelo->meshes.size())
                            return;
                        auto updated =
                            renderer->m_modelo->meshes[index].getMaterial();
                        updated.set_uniform("material.albedo", value);
                        renderer->m_modelo->meshes[index].setMaterial(updated);
                    };
                    apply_color(next);
                    m_context->record_action(
                        [apply_color, previous]() { apply_color(previous); },
                        [apply_color, next]() { apply_color(next); }
                    );
                }
            );

            for (const auto& factor : commons::pbr_factors) {
                add_number_field(section.children(), factor.label,
                    commons::pbr_value(material, factor),
                    [renderer, index, factor]() {
                        return commons::pbr_value(renderer->m_modelo->meshes[index].getMaterial(), factor);
                    },
                    [renderer, index, factor](float value) {
                        auto updated = renderer->m_modelo->meshes[index].getMaterial();
                        updated.set_uniform(std::string("material.") + factor.name,
                            std::clamp(value, 0.f, 1.f));
                        renderer->m_modelo->meshes[index].setMaterial(updated);
                    });
            }
            auto& world = section.children().add<bgui::checkbox>(
                "WorldTexture", 0.29f, commons::world_texture(material));
            world.set_on_change([this, renderer, index](bool enabled) {
                auto apply = [renderer, index](bool value) {
                    auto updated = renderer->m_modelo->meshes[index].getMaterial();
                    updated.set_uniform("uvMundo", value);
                    renderer->m_modelo->meshes[index].setMaterial(updated);
                };
                const bool before = commons::world_texture(renderer->m_modelo->meshes[index].getMaterial());
                apply(enabled);
                m_context->record_action([apply, before]() { apply(before); },
                    [apply, enabled]() { apply(enabled); });
            });
            for (const auto& factor : commons::texture_factors) {
                add_number_field(section.children(), factor.label,
                    commons::pbr_value(material, factor),
                    [renderer, index, factor]() {
                        return commons::pbr_value(renderer->m_modelo->meshes[index].getMaterial(), factor);
                    },
                    [renderer, index, factor](float value) {
                        auto updated = renderer->m_modelo->meshes[index].getMaterial();
                        updated.set_uniform(std::string("material.") + factor.name, value);
                        renderer->m_modelo->meshes[index].setMaterial(updated);
                    });
            }
            for (const auto& map : commons::pbr_maps)
                add_material_map(section.children(), renderer, index, map);
        }
    }

    void add_material_map(bgui::linear& content,
        const std::shared_ptr<commons::renderer>& renderer, std::size_t index,
        const commons::pbr_map& map) {
        const auto material = renderer->m_modelo->meshes[index].getMaterial();
        auto& row = content.add<bgui::linear>(bgui::orientation::horizontal);
        configure_property_row(row);
        auto& label = row.add<bgui::text>(map.label, 0.27f);
        configure_property_label(label);
        auto& input = row.add<bgui::inputbox>(material.getTextura(map.name).path,
            "Drop a texture or enter its path", 0.26f);
        configure_property_input(input);
        auto& preview = content.add<bgui::image>();
        preview.add_class("material-texture-preview");
        preview.style.layout.require_size(96.f, 72.f);
        auto show_preview = [this, preview = &preview](const std::string& value) {
            preview->set_enable(false);
            if (value.empty()) return;
            try {
                const std::filesystem::path path(value);
                auto& texture = bgui::asset_manager::get_instance().load_texture(
                    (path.is_absolute() ? path : m_context->project_root / path).string());
                preview->set_texture(texture);
                preview->set_enable(true);
            } catch (const std::exception&) {
                // Keep the path editable when a referenced texture is unavailable.
            }
        };
        show_preview(material.getTextura(map.name).path);
        auto assign = [this, renderer, index, map, show_preview, input = &input](const std::filesystem::path& requested) {
            if (!renderer->m_modelo || index >= renderer->m_modelo->meshes.size())
                return;
            auto before = renderer->m_modelo->meshes[index].getMaterial();
            auto after = before;
            if (requested.empty()) {
                after.textures.erase(map.name);
                after.set_uniform(std::string("use_") + map.name, false);
            } else {
                auto path = requested.is_absolute() ? requested : m_context->project_root / requested;
                try {
                    auto& texture = bgui::asset_manager::get_instance().load_texture(path.string());
                    after.set_texture(map.name, commons::texture{0, texture.m_path});
                    after.set_uniform(std::string("use_") + map.name, true);
                } catch (const std::exception& error) {
                    m_edit_status->set_buffer(error.what());
                    return;
                }
            }
            auto apply = [renderer, index](const commons::material& value) {
                if (renderer->m_modelo && index < renderer->m_modelo->meshes.size())
                    renderer->m_modelo->meshes[index].setMaterial(value);
            };
            apply(after);
            m_context->record_action([apply, before]() { apply(before); },
                [apply, after]() { apply(after); });
            input->set_buffer(after.getTextura(map.name).path);
            show_preview(after.getTextura(map.name).path);
            m_edit_status->set_buffer("");
        };
        input.m_enter_func = [assign](const std::string& path) { assign(path); };
        m_asset_targets.push_back({&input, assign});
        auto& clear = row.add<bgui::button>("", 0.25f, [assign]() { assign({}); });
        clear.add_class("editor-icon-button");
        clear.style.layout.require_size(22.f, 22.f);
        clear.get_label().set_enable(false);
        clear.add<bgui::icon>("close", 0.25f).receives_input(false);
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
            auto& input = section.children().add<bgui::inputbox>(
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
            auto& checkbox = content.add<bgui::checkbox>(property.name, .29f, std::get<bool>(property.value));
            checkbox.set_on_change([change](bool value) { change(value); });
        } else if (property.type == "color") {
            content.add<bgui::text>(property.name, .27f);
            const auto color = std::get<std::array<float, 4>>(property.value);
            auto& picker = content.add<bgui::color_picker>(bgui::color{color[0], color[1], color[2], color[3]});
            picker.add_class("editor-input");
            picker.style.layout.require_size(128.f, 92.f);
            picker.set_on_change([change](const bgui::color& value) {
                change(std::array<float, 4>{value.x, value.y, value.z, value.w});
            });
            auto& alpha = content.add<bgui::field>(property.name + " Alpha", std::to_string(color[3]), .27f,
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
            auto& field = content.add<bgui::field>(property.name, buffer, .27f, assign);
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
            true,
            [this, id]() { remove_component<commons::camera>(id); }
        );
        auto& content = section.children();

        add_number_field(
            content,
            "Field of View",
            camera->fov,
            [camera]() { return camera->fov; },
            [camera](float value) { camera->fov = value; }
        );
        add_number_field(
            content,
            "Far Clip",
            camera->corte_longo,
            [camera]() { return camera->corte_longo; },
            [camera](float value) { camera->corte_longo = value; }
        );
        add_number_field(
            content,
            "Orthographic Scale",
            camera->scale,
            [camera]() { return camera->scale; },
            [camera](float value) { camera->scale = value; }
        );

        auto& orthographic = content.add<bgui::checkbox>(
            "Orthographic",
            0.29f,
            camera->flag_orth
        );
        orthographic.set_on_change(
            [this, camera](bool enabled) {
                const bool previous = camera->flag_orth;
                if (previous == enabled)
                    return;
                camera->flag_orth = enabled;
                m_context->record_action(
                    [camera, previous]() { camera->flag_orth = previous; },
                    [camera, enabled]() { camera->flag_orth = enabled; }
                );
            }
        );

    }

    template<class Content, class Component>
    void add_shadow_toggle(Content& content, const char* label,
        std::shared_ptr<Component> component, bool Component::*field) {
        auto& checkbox = content.template add<bgui::checkbox>(label, 0.29f, component.get()->*field);
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

        add_shadow_toggle(content, "Shadows", light, &commons::directional_light::shadows);
        add_number_field(content, "Shadow Resolution", static_cast<float>(light->shadow_resolution),
            [light]() { return static_cast<float>(light->shadow_resolution); },
            [light](float v) { light->shadow_resolution = static_cast<int>(std::clamp(v, 128.f, 8192.f)); });
        add_number_field(content, "Shadow Distance", light->shadow_distance,
            [light]() { return light->shadow_distance; }, [light](float v) { light->shadow_distance = v; });
        add_number_field(content, "Shadow Bias", light->shadow_bias,
            [light]() { return light->shadow_bias; }, [light](float v) { light->shadow_bias = v; });
        add_number_field(content, "Shadow Strength", light->shadow_strength,
            [light]() { return light->shadow_strength; }, [light](float v) { light->shadow_strength = v; });
        add_vector_field(
            content,
            "Direction",
            light->direction,
            [light]() { return light->direction; },
            [light](std::size_t axis, float value) {
                light->direction[axis] = value;
            }
        );
        add_vector_field(
            content,
            "Ambient",
            light->ambient,
            [light]() { return light->ambient; },
            [light](std::size_t axis, float value) {
                light->ambient[axis] = value;
            }
        );
        add_vector_field(
            content,
            "Color",
            light->color,
            [light]() { return light->color; },
            [light](std::size_t axis, float value) {
                light->color[axis] = value;
            }
        );
        add_number_field(
            content,
            "Intensity",
            light->intensity,
            [light]() { return light->intensity; },
            [light](float value) { light->intensity = value; }
        );
    }

    void add_vector_field(
        bgui::linear& parent,
        const std::string& label,
        const bgui::vec3& value,
        const std::function<bgui::vec3()>& get_value,
        const std::function<void(std::size_t, float)>& on_change
    ) {
        auto current_value = std::make_shared<bgui::vec3>(value);
        auto& row = parent.add<bgui::linear>(bgui::orientation::horizontal);
        configure_property_row(row);
        auto& name = row.add<bgui::text>(label, 0.27f);
        configure_property_label(name);
        auto& values = row.add<bgui::linear>(bgui::orientation::horizontal);
        values.add_class("editor-property-row");
        configure_row(values);
        values.style.layout.require_width(bgui::mode::stretch);
        values.style.layout.padding = bgui::vec4i{0, 0, 0, 0};
        values.style.layout.margin = bgui::vec4i{0, 0, 0, 0};

        constexpr const char* axes[]{"X", "Y", "Z"};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            auto& axis_label = values.add<bgui::text>(axes[axis], 0.27f);
            axis_label.add_class("editor-property-label");
            axis_label.style.layout.require_width(bgui::mode::pixel, 12.f);

            auto& input = values.add<bgui::inputbox>(
                format_value(value[axis]),
                "",
                0.27f,
                [this, on_change, current_value, axis, label](
                    const std::string& buffer
                ) {
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
                    const float previous = (*current_value)[axis];
                    if (previous == parsed)
                        return;
                    auto apply_value = [on_change, current_value, axis](
                        const float next
                    ) {
                        on_change(axis, next);
                        (*current_value)[axis] = next;
                    };
                    apply_value(parsed);
                    m_context->record_action(
                        [apply_value, previous]() { apply_value(previous); },
                        [apply_value, parsed]() { apply_value(parsed); }
                    );
                },
                bgui::input_mode::inputbox
            );
            configure_property_input(input);
        }
        *current_value = get_value();
    }

    void add_number_field(
        bgui::linear& parent,
        const std::string& label,
        const float value,
        const std::function<float()>& get_value,
        const std::function<void(float)>& on_change
    ) {
        auto current_value = std::make_shared<float>(value);
        auto& field = parent.add<bgui::field>(
            label,
            format_value(value),
            0.27f,
            [this, on_change, current_value, label](const std::string& buffer) {
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
                const float previous = *current_value;
                if (previous == parsed)
                    return;
                auto apply_value = [on_change, current_value](float next) {
                    on_change(next);
                    *current_value = next;
                };
                apply_value(parsed);
                m_context->record_action(
                    [apply_value, previous]() { apply_value(previous); },
                    [apply_value, parsed]() { apply_value(parsed); }
                );
            }
        );
        configure_property_field(field);
        *current_value = get_value();
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
