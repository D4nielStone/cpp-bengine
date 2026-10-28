#pragma once

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
#include <elem/color_picker.hpp>
#include <elem/field.hpp>
#include <elem/icon.hpp>
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
        m_entity_tree = &m_root->add<bgui::tree>(
            "Components",
            0.34f,
            true
        );
        m_entity_tree->add_class("editor-header");
        m_entity_tree->set_icon("home", 0.34f);
        m_entity_tree->style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        m_entity_tree->children().add_class("editor-surface");

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

        if (auto physics = registry->get<commons::physics>(id)) {
            auto& section = add_section("Physics", true,
                [this, id]() { remove_component<commons::physics>(id); });
            add_number_field(section.children(), "Mass", physics->m_massa,
                [physics]() { return physics->m_massa; },
                [physics](float mass) { physics->setMass(std::max(0.f, mass)); });
        }
        add_script(id, registry->get<commons::code>(id));
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

        auto& add_component_button = m_toolbar->add<bgui::button>(
            "+ Add Component",
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
        m_component_options = &m_toolbar->add<bgui::linear>(
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
        m_component_options->add<bgui::text>("Script: drop a .lua file onto an entity", 0.25f);
        m_edit_status = &m_toolbar->add<bgui::text>("", 0.25f);
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
        auto& model_field = section.children().add<bgui::field>(
            "Model", renderer->m_modelo ? renderer->m_modelo->get_source_path() : "", 0.27f);
        auto* model_input = &model_field.get_inputbox();
        auto assign_model = [this, id, renderer](const std::filesystem::path& requested) {
            auto path = requested.is_absolute() ? requested : m_context->project_root / requested;
            try {
                auto model = std::make_unique<commons::model>(path.string());
                if (model->meshes.empty()) {
                    m_edit_status->set_buffer("Could not load model: " + requested.string());
                    return;
                }
                auto registry = m_context->registry;
                auto next = std::make_shared<commons::renderer>(model.release());
                next->my_object = id;
                next->reg = registry;
                next->visible = renderer->visible;
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
            for (const auto& map : commons::pbr_maps)
                add_material_map(section.children(), renderer, index, map);
        }
    }

    void add_material_map(bgui::linear& content,
        const std::shared_ptr<commons::renderer>& renderer, std::size_t index,
        const commons::pbr_map& map) {
        const auto material = renderer->m_modelo->meshes[index].getMaterial();
        auto& label = content.add<bgui::text>(map.label, 0.27f);
        label.add_class("editor-property-label");
        auto& row = content.add<bgui::linear>(bgui::orientation::horizontal);
        configure_row(row);
        auto& input = row.add<bgui::inputbox>(material.getTextura(map.name).path,
            "Drop a texture or enter its path", 0.26f);
        input.style.layout.require_width(bgui::mode::stretch);
        auto assign = [this, renderer, index, map, input = &input](const std::filesystem::path& requested) {
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
        auto& section = add_section("Lua Script", static_cast<bool>(script),
            [this, id]() { remove_component<commons::code>(id); });
        auto& input = section.children().add<bgui::inputbox>(
            script ? script->file : "", "Drop a Lua script or enter its path", 0.27f);
        input.style.layout.require_width(bgui::mode::match_parent);
        auto assign = [this, id](const std::filesystem::path& path) {
            std::string error;
            if (attach_lua_script(m_context, id, path, error)) refresh();
            else m_edit_status->set_buffer(error);
        };
        input.m_enter_func = [assign](const std::string& path) { assign(path); };
        m_asset_targets.push_back({&input, assign});
        auto& hint = section.children().add<bgui::text>(
            "Drop onto an entity in Scene or Hierarchy. Replacing a script supports Undo.", 0.25f);
        hint.add_class("editor-muted");
        hint.style.layout.require_width(bgui::mode::match_parent);
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
            input.add_class("editor-input");
            input.style.layout.require_width(bgui::mode::stretch);
            input.style.layout.require_height(bgui::mode::pixel, 22.f);
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
            0.29f,
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
        field.add_class("editor-input");
        field.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
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
