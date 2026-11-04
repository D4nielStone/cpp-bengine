#pragma once

#include "editable_model_import.hpp"
#include "mesh_topology_edit.hpp"
#include "model_mesh_extract.hpp"
#include "editor_context.hpp"
#include "panel.hpp"
#include "components/renderer.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "viewport_input_state.hpp"
#include "../tools/camera_movement.hpp"
#include "mesh_material_editor.hpp"
#include "../tools/direction_gizmo.hpp"
#include "loaders/material_asset.hpp"
#include "render_viewport.hpp"
#include <chrono>
#include <elem/tree.hpp>
#include <elem/combo_box.hpp>
#include <elem/modal.hpp>

#include <elem/image.hpp>
#include <elem/inputbox.hpp>
#include <elem/text.hpp>
#include <elem/window.hpp>
#include <lay/linear.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace editor::ui {

enum class model_selection_mode {
    object,
    faces,
    vertices,
    edges
};

class model_editor_view final : public panel {
public:
    explicit model_editor_view(std::shared_ptr<editor_context> context)
        : m_context(std::move(context)) {}

    void build(bgui::dock& parent) override {
        if (m_window)
            return;

        m_window = &parent.add_window(
            "Edição do modelo",
            bgui::dock_area::center,
            0.3f
        );
        m_window->add_class("editor-panel");
        auto& root = m_window->add<model_surface>(*this);
        root.add_class("editor-surface");
        root.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        root.style.layout.padding = bgui::vec4i{0};

        auto& identity = root.add<bgui::linear>(bgui::orientation::horizontal);
        identity.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        identity.style.layout.require_height(bgui::mode::pixel, 30.f);
        identity.style.layout.padding = bgui::vec4i{4};
        identity.style.visual.background.normal = bgui::color{.12f, .12f, .12f, 1.f};
        m_selection_label = &identity.add<bgui::text>("Nenhum objeto selecionado", .30f);
        m_selection_label->style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);
        m_selection_label->set_wrap(false);
        m_selection_label->receives_input(false);
        m_source_label = &identity.add<bgui::text>("Selecione um objeto na Hierarquia.", .25f);
        m_source_label->style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);
        m_source_label->set_wrap(false);
        m_source_label->style.visual.text.normal = bgui::color{.6f, .6f, .6f, 1.f};
        m_source_label->receives_input(false);

        auto& modes = root.add<bgui::linear>(bgui::orientation::horizontal);
        modes.add_class("model-selection-toolbar");
        modes.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        modes.style.layout.require_height(bgui::mode::pixel, 34.f);
        modes.style.layout.padding = bgui::vec4i{2};
        modes.style.visual.background.normal = bgui::color{.16f, .16f, .16f, 1.f};
        add_mode_button(modes, "Objeto", model_selection_mode::object);
        add_mode_button(modes, "Faces", model_selection_mode::faces);
        add_mode_button(modes, "Vértices", model_selection_mode::vertices);
        add_mode_button(modes, "Arestas", model_selection_mode::edges);

        auto& views = root.add<bgui::linear>(bgui::orientation::horizontal);
        views.add_class("model-shading-toolbar");
        views.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        views.style.layout.require_height(bgui::mode::pixel, 30.f);
        views.style.layout.padding = bgui::vec4i{2};
        views.style.visual.background.normal = bgui::color{.13f, .13f, .13f, 1.f};
        for (int mode = 0; mode < 3; ++mode) {
            const char* names[]{"Sólido", "Material", "Render"};
            auto& button = views.add<bgui::button>(names[mode], .26f, [this, mode] {
                m_context->model_view_mode = mode;
            });
            button.style.layout.require_mode(bgui::mode::stretch, bgui::mode::pixel);
            button.style.layout.require_height(bgui::mode::pixel, 26.f);
            button.style.layout.margin = bgui::vec4i{0, 2, 0, 2};
            m_view_buttons.push_back(&button);
        }
        auto& body = root.add<bgui::linear>(bgui::orientation::horizontal);
        body.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        body.style.layout.padding = bgui::vec4i{0};
        m_image = &body.add<bgui::image>();
        m_image->add_class("model-editor-preview");
        m_image->add_class("editor-viewport-surface");
        m_image->style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);
        m_image->style.visual.background.normal = bgui::color{.055f, .06f, .07f, 1.f};
        m_image->receives_input(false);
        m_pick_overlay = &m_window->add<pick_overlay, bgui::layer::overlay>(*this, *m_image);

        auto& mesh_window = parent.add_window("Malhas", bgui::dock_area::right, .27f);
        mesh_window.add_class("editor-panel");
        auto& tools = mesh_window.add<bgui::linear>(bgui::orientation::vertical);
        tools.add_class("editor-surface");
        tools.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        tools.style.layout.padding = bgui::vec4i{12, 8, 4, 8};
        tools.set_scrollable(true);
        tools.add<bgui::text>("GEOMETRIA", .28f).receives_input(false);
        m_mesh_label = &tools.add<bgui::text>("Nenhum modelo", .26f);
        m_mesh_label->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_mesh_label->receives_input(false);
        tools.add<bgui::text>("Distância de extrusão", .26f).receives_input(false);
        m_extrude_distance = &tools.add<bgui::inputbox>("0.25", "Distância", .28f);
        m_extrude_distance->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        m_extrude_distance->style.layout.require_height(bgui::mode::pixel, 28.f);
        m_extrude_distance->style.layout.limit_min = {0, 28};
        m_extrude_distance->style.layout.limit_max = {10000, 28};
        auto& actions = tools.add<bgui::linear>(bgui::orientation::vertical);
        actions.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        actions.style.layout.padding = bgui::vec4i{0};
        auto& undo_button = actions.add<bgui::button>("Desfazer", .28f, [this] { restore_geometry(false); });
        auto& redo_button = actions.add<bgui::button>("Refazer", .28f, [this] { restore_geometry(true); });
        m_extrude_button = &actions.add<bgui::button>("Extrudar face", .28f, [this] { extrude(); });
        auto& group_button = actions.add<bgui::button>("Agrupar faces", .28f, [this] {
            if (m_gizmo.is_dragging() || m_extrusion_before || m_cut_before || !is_native_model(m_source_path) ||
                m_selected_faces.empty() || m_active_mesh >= m_model.meshes.size()) return;
            auto before = capture_geometry();
            auto& groups = m_model.meshes[m_active_mesh].face_groups;
            groups.push_back({"Grupo " + std::to_string(groups.size() + 1),
                {m_selected_faces.begin(), m_selected_faces.end()}});
            record_geometry(std::move(before));
            update_mesh_label();
            set_status("Grupo de faces criado. Salve o modelo.");
        });
        m_loop_button = &actions.add<bgui::button>("Loop Cut", .28f, [this] { start_loop_cut(); });
        auto& save_button = actions.add<bgui::button>("Salvar .bmodel", .28f, [this] { save(); });
        auto& export_button = actions.add<bgui::button>("Extract (Ctrl+E)", .28f, [this] { request_extract(); });
        for (auto* button : {&undo_button, &redo_button, m_extrude_button, &group_button, m_loop_button, &save_button, &export_button}) {
            button->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
            button->style.layout.require_height(bgui::mode::pixel, 30.f);
            button->style.layout.margin = bgui::vec4i{2, 0, 2, 0};
        }
        tools.add<bgui::text>("NAVEGAÇÃO", .28f).receives_input(false);
        m_navigation_button = &tools.add<bgui::button>("Visão: Normal (O)", .28f, [this] {
            m_context->orbital_view = !m_context->orbital_view;
        });
        m_navigation_button->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::pixel);
        m_navigation_button->style.layout.require_height(bgui::mode::pixel,30.f);
        auto& frame = tools.add<bgui::button>("Enquadrar modelo", .28f, [this] {
            m_frame_preview = true;m_projection_dirty=true;
            mark_preview_dirty();
        });
        frame.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        frame.style.layout.require_height(bgui::mode::pixel, 30.f);
        auto& help = tools.add<bgui::text>(
            "O: visão normal/orbital.\nBotão direito: olhar/orbitar.\nCtrl + Shift + arraste: pan.\nScroll: aproximar/afastar.\nTab: modo objeto/edição.\nShift + clique: seleção múltipla.\nArraste para selecionar por retângulo. Shift + arraste adiciona.\nDelete: apagar seleção.\nCtrl+C / Ctrl+V: copiar / colar elementos.\nCtrl+S: salvar. Ctrl+E: Extract.\nG/R/S: mover/girar/escalar.\nE: extrudar faces. Ctrl + R: Loop Cut.\n\nFaces: selecione para extrudar.\nArestas: selecione para Loop Cut.\n\nAs alterações ficam pendentes até salvar.", .25f);
        help.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        help.receives_input(false);
        m_mesh_controls = &tools.add<bgui::linear>(bgui::orientation::vertical);
        m_mesh_controls->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
        m_status = &root.add<bgui::text>("Selecione um objeto com modelo 3D.", .26f);
        m_status->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_status->receives_input(false);
        m_preview_registry = std::make_shared<commons::ecs>();
        auto camera_entity = m_preview_registry->create();
        m_preview_registry->add<commons::camera>(camera_entity);
        m_preview_camera = m_preview_registry->get<commons::camera>(camera_entity.id);
        m_navigation = std::make_unique<camera_movement_system>(m_preview_camera, *m_image, m_navigation_input);
        m_navigation->set_primary_drag_enabled(false);
        m_navigation->setup(m_preview_registry);
        m_window->add<direction_gizmo, bgui::layer::overlay>(m_preview_camera, *m_image);
        refresh_mode_buttons();
        refresh_preview();
    }

    const commons::camera& preview_camera() const { return *m_preview_camera; }
    double last_preview_time_ms() const noexcept { return m_last_preview_ms; }
    std::size_t geometry_upload_count() const noexcept { return m_geometry_uploads; }
    std::size_t projection_update_count() const noexcept { return m_projection_updates; }

    bool handle_history_shortcut(bool redo) {
        if (!m_image || m_context->is_playing || !viewport_owns_mouse(m_image) ||
            !is_native_model(m_source_path)) return false;
        if (m_geometry_undo.empty() && m_geometry_redo.empty() &&
            !m_gizmo.is_dragging() && !m_extrusion_before && !m_cut_before) return false;
        restore_geometry(redo);
        return true;
    }

    void set_extract_callback(std::function<void(const std::string&)> callback) {
        m_on_extract = std::move(callback);
    }

    void request_extract() {
        bgui::add_function([this] { show_extract(); });
    }

    void copy_selection() {
        if (geometry_operation_active()) return;
        sync_selected_entity();
        if (m_mode == model_selection_mode::object || m_active_mesh >= m_model.meshes.size()) {
            set_status("Selecione faces, vértices ou arestas para copiar.");
            return;
        }

        const std::set<std::size_t> empty_indices;
        const std::set<mesh_edge> empty_edges;
        auto copy = copy_mesh_elements(m_model.meshes[m_active_mesh],
            m_mode == model_selection_mode::faces ? m_selected_faces : empty_indices,
            m_mode == model_selection_mode::vertices ? m_selected_vertices : empty_indices,
            m_mode == model_selection_mode::edges ? m_selected_edges : empty_edges);
        if (copy.geometry.vertices.empty()) {
            set_status("Selecione elementos para copiar.");
            return;
        }
        m_clipboard = std::move(copy);
        m_clipboard_project = m_context->project_root;
        m_clipboard_mode = m_mode;
        set_status("Elementos copiados.");
    }

    void paste_selection() {
        if (geometry_operation_active()) return;
        sync_selected_entity();
        if (m_active_mesh >= m_model.meshes.size() || !is_native_model(m_source_path)) {
            set_status("Abra um modelo .bmodel para colar os elementos.");
            return;
        }
        if (m_clipboard.geometry.vertices.empty() || m_clipboard_project != m_context->project_root) {
            set_status("Copie elementos no projeto atual antes de colar.");
            return;
        }

        auto before = capture_geometry();
        auto& mesh = m_model.meshes[m_active_mesh];
        const auto vertex_offset = static_cast<unsigned int>(mesh.vertices.size());
        const auto face_offset = mesh.faces.size();
        std::string error;
        if (!append_mesh_elements(mesh, m_clipboard, error)) {
            set_status(error);
            return;
        }

        set_selection_mode(m_clipboard_mode);
        if (m_mode == model_selection_mode::vertices)
            for (auto i = vertex_offset; i < mesh.vertices.size(); ++i) m_selected_vertices.insert(i);
        if (m_mode == model_selection_mode::faces)
            for (auto i = face_offset; i < mesh.faces.size(); ++i) m_selected_faces.insert(i);
        for (const auto& [a, b] : m_clipboard.edges)
            m_selected_edges.emplace(a + vertex_offset, b + vertex_offset);
        record_geometry(std::move(before));
        m_geometry_dirty = m_projection_dirty = m_gizmo_selection_dirty = true;
        update_mesh_label();
        set_status("Elementos colados na malha ativa. Salve o modelo.");
        mark_preview_dirty();
    }

    bool drop_asset(const std::filesystem::path& path, const bgui::vec2i& position) {
        for (const auto& [element, assign] : m_material_targets) {
            bool enabled = true;
            for (auto* current = element; current; current = current->get_parent()) enabled &= current->is_enabled();
            const auto r = element->processed_rect();
            if (enabled && position.x >= r.x && position.x < r.x+r.z && position.y >= r.y && position.y < r.y+r.w) {
                assign(path); return true;
            }
        }
        return false;
    }

    void update() override {
        if (m_context && !m_extrusion_before && !m_cut_before && static_cast<int>(m_mode) != m_context->model_selection) {
            m_gizmo_selection_dirty=true;
            m_mode = static_cast<model_selection_mode>(m_context->model_selection);
            if (m_mode != model_selection_mode::object) m_last_edit_mode = m_mode;
            m_picking_cut = false;
            m_selection_start.reset();
            refresh_mode_buttons();
            mark_preview_dirty();
        }
        if (m_context && m_extrude_distance) {
            if (m_context->extrude_distance != m_last_extrude_distance)
                m_extrude_distance->set_buffer(m_context->extrude_distance);
            else
                m_context->extrude_distance = m_extrude_distance->get_buffer();
            m_last_extrude_distance = m_context->extrude_distance;
        }
        sync_selected_entity();
        sync_model_transform();
        if (m_view_mode!=m_context->model_view_mode) {
            m_view_mode = std::clamp(m_context->model_view_mode, 0, 2);
            m_material_dirty = true;
            mark_preview_dirty();
            for(std::size_t i=0;i<m_view_buttons.size();++i) {
                m_view_buttons[i]->style.visual.background.normal=static_cast<int>(i)==m_view_mode
                    ? bgui::color{.17f,.36f,.58f,1.f}:bgui::color{.12f,.12f,.12f,1.f};
                m_view_buttons[i]->mark_style_dirty();
            }
        }
        const auto history = m_context->history_revision;
        const auto actions = m_context->undo_stack.size();
        if (history != m_material_revision || actions != m_material_actions) {
            const bool editing = bgui::get_pressed(bgui::input_key::mouse_left) ||
                dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus());
            if (history != m_material_revision || !editing) rebuild_mesh_controls();
            m_material_actions = actions;
            m_material_revision = history;
            m_material_dirty=true;
            mark_preview_dirty();
            for (auto& [input, read] : m_material_inputs)
                if (bgui::get_keyboard_focus() != input) input->set_buffer(read());
        }
        if (m_navigation && !m_model.meshes.empty()) {
            const auto& preferences = m_context->preferences;
            m_navigation->set_movement_speed(preferences.movement_speed);
            m_navigation->set_sprint_multiplier(preferences.sprint_multiplier);
            m_navigation->set_mouse_sensitivity(preferences.mouse_sensitivity);
            m_navigation->set_smoothing_speed(preferences.smoothing_speed);
            const bool over = mouse_over_viewport(m_image) && !viewport_input_blocked_by_modal() &&
                !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus());
            const bool key = bgui::get_pressed(bgui::input_key::o);
            if (key && !m_orbit_key_down && over) m_context->orbital_view = !m_context->orbital_view;
            m_orbit_key_down = key;
            if (m_navigation->is_orbital() != m_context->orbital_view) {
                if (m_context->orbital_view) {
                    auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
                    m_navigation->focus_orbit_target(m_orbit_pivot, (eye->get_position()-m_orbit_pivot->get_position()).length());
                } else m_navigation->set_orbital(false);
            }
            m_navigation_button->get_label().set_buffer(m_context->orbital_view ? "Visão: Orbital (O)" : "Visão: Normal (O)");
            auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
            const auto pivot = m_context->orbital_view ? m_orbit_pivot->get_position() : m_model_center;
            const float zoom_distance = std::clamp((eye->get_position() - pivot).length(),
                m_frame_distance * .01f, m_frame_distance * 10.f);
            m_navigation->set_zoom_sensitivity(preferences.zoom_sensitivity * zoom_distance * .15f);
            const auto before = eye->get_model_matrix();
            m_navigation_input->gizmo_active = m_gizmo.is_dragging() || m_extrusion_before.has_value() ||
                m_cut_before.has_value() || m_selection_start.has_value();
            const bool control = bgui::get_pressed(bgui::input_key::left_control) ||
                bgui::get_pressed(bgui::input_key::right_control);
            m_navigation_input->movement_shortcut_active = control &&
                (bgui::get_pressed(bgui::input_key::s) || bgui::get_pressed(bgui::input_key::e));
            m_navigation->update(m_preview_registry);
            if (before != eye->get_model_matrix()) { m_preview_dirty = true; m_projection_dirty=true; }
        }
        if (m_image && !m_model.meshes.empty()) {
            const int width = std::clamp(
                m_image->processed_width(), 240, 1280
            );
            const int height = std::clamp(
                m_image->processed_height(), 180, 900
            );
            if (static_cast<int>(m_image->get_texture().m_size.x) != width ||
                static_cast<int>(m_image->get_texture().m_size.y) != height)
                { m_preview_dirty = true; m_projection_dirty=true; }
        }
        if (m_view_mode==2 && gpu_available() && !m_model.meshes.empty()) m_preview_dirty=true;
        if (m_preview_dirty)
            refresh_preview();
    }

private:
    class model_surface final : public bgui::linear {
        model_editor_view& m_owner;
    public:
        explicit model_surface(model_editor_view& owner)
            : bgui::linear(bgui::orientation::vertical), m_owner(owner) {}
        void on_update() override {
            m_owner.update();
            bgui::linear::on_update();
        }
    };

    class pick_overlay final : public bgui::element {
    public:
        pick_overlay(model_editor_view& owner, bgui::image& image)
            : m_owner(owner), m_image(image) {
            type = "model-editor-picking-overlay";
            add_class("model-editor-viewport-input");
            receives_input(true);
            set_flex(false);
        }

        void on_update() override {
            const auto rect = m_image.processed_rect();
            set_final_rect(rect.x, rect.y, rect.z, rect.w);
        }

        bool supports_render_cache() const override { return false; }
        void get_requires(bgui::draw_data*) override { m_owner.draw_gizmo(); }

    private:
        model_editor_view& m_owner;
        bgui::image& m_image;
    };

    bgui::window* m_window{nullptr};
    bgui::image* m_image{nullptr};
    bgui::element* m_pick_overlay{nullptr};
    bgui::inputbox* m_extrude_distance{nullptr};
    bgui::text* m_mesh_label{nullptr};
    bgui::text* m_selection_label{nullptr};
    bgui::text* m_source_label{nullptr};
    bgui::button* m_extrude_button{nullptr};
    bgui::button* m_loop_button{nullptr};

    std::weak_ptr<commons::renderer> m_source_renderer;
    bgui::text* m_status{nullptr};
    std::shared_ptr<editor_context> m_context;
    commons::editable_model_data m_model;
    std::filesystem::path m_source_path;
    std::optional<editor_context::entity_id> m_model_entity;
    std::vector<bgui::button*> m_mode_buttons;
    std::vector<std::vector<bgui::vec3>> m_projected_depths;
    std::vector<std::vector<bgui::vec2>> m_projected_vertices;
    std::set<std::size_t> m_selected_faces;
    std::set<std::size_t> m_selected_vertices;
    mesh_element_clipboard m_clipboard;
    std::filesystem::path m_clipboard_project;
    model_selection_mode m_clipboard_mode{model_selection_mode::vertices};
    std::function<void(const std::string&)> m_on_extract;
    std::set<mesh_edge> m_selected_edges;
    std::optional<bgui::vec2i> m_selection_start;
    bool m_selection_extend{};
    bool m_delete_down{};
    glm::mat4 m_model_world{1.f};
    std::optional<glm::mat4> m_world_to_model{glm::mat4{1.f}};
    model_selection_mode m_mode{model_selection_mode::object};
    std::size_t m_active_mesh{0};
    std::uint64_t m_preview_revision{0};
    bool m_object_selected{false};
    bool m_preview_dirty{true};
    bool m_dirty{false};
    std::string m_last_extrude_distance;
    std::vector<bgui::button*> m_view_buttons;
    int m_view_mode{-1};
    bool m_geometry_dirty{true},m_material_dirty{true},m_projection_dirty{true};
    std::shared_ptr<commons::renderer> m_gpu_renderer;
    commons::render_system m_gpu_render_system;
    commons::camera m_scene_preview_camera;
    std::optional<commons::shader> m_basic_shader;
    std::size_t m_geometry_uploads{0},m_projection_updates{0};
    double m_last_preview_ms{0};

    bgui::vec3 world_point(const bgui::vec3& point) const {
        const auto world = m_model_world * glm::vec4(point.x, point.y, point.z, 1.f);
        return {world.x, world.y, world.z};
    }

    void sync_model_transform() {
        glm::mat4 world{1.f};
        if (m_model_entity && m_context->registry) {
            if (const auto transform = m_context->registry->get<commons::transform>(*m_model_entity))
                world = transform->get_world_model_matrix();
        }
        if (world == m_model_world) return;
        m_model_world = world;
        m_world_to_model.reset();
        if (glm::determinant(world) != 0.f) {
            const auto inverse = glm::inverse(world);
            bool finite = true;
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                    finite &= std::isfinite(inverse[col][row]);
            if (finite) m_world_to_model = inverse;
        }
        if (m_drag_before) {
            record_geometry(std::move(*m_drag_before));
            m_drag_before.reset();
        }
        m_gizmo = editor::transform_gizmo{};
        m_frame_preview = m_projection_dirty = m_gizmo_selection_dirty = true;
        mark_preview_dirty();
    }

    static bool gpu_available() { return glad_glGetString && glGetString(GL_VERSION); }
    void sync_gpu_model() {
        if (!m_gpu_renderer) {
            auto entity=m_preview_registry->create();
            auto model=std::make_unique<commons::model>();
            m_basic_shader=model->getShader();
            m_preview_registry->add<commons::renderer>(entity,model.release());
            m_gpu_renderer=m_preview_registry->get<commons::renderer>(entity.id);
        }
        if (m_geometry_dirty) {
            auto& meshes=m_gpu_renderer->m_modelo->meshes;
            for(auto& mesh:meshes) if(mesh.isLoaded())mesh.unload();
            meshes.clear();meshes.reserve(m_model.meshes.size());
            for (const auto& mesh : m_model.meshes)
                meshes.emplace_back(mesh.vertices, mesh.triangulated_indices(), commons::material{});
            m_geometry_dirty=false;m_material_dirty=true;++m_geometry_uploads;
        }
        if (m_material_dirty) {
            const auto source=m_source_renderer.lock();
            if (source && source->m_modelo) {
                m_gpu_renderer->visible=m_view_mode==2 ? source->visible : true;
                m_gpu_renderer->cast_shadows=source->cast_shadows;
                m_gpu_renderer->receive_shadows=source->receive_shadows;
                m_gpu_renderer->m_modelo->setShader(m_view_mode==0?*m_basic_shader:source->m_modelo->getShader());
                for(std::size_t i=0;i<m_gpu_renderer->m_modelo->meshes.size();++i) {
                    auto material=i<source->m_modelo->meshes.size()?source->m_modelo->meshes[i].getMaterial():commons::material{};
                    if(m_view_mode==0) {
                        material=commons::material{};
                        material.set_uniform("material.albedo",commons::color{.65f,.65f,.65f,1.f});
                        material.set_uniform("material.metallic",0.f);material.set_uniform("material.roughness",.7f);material.set_uniform("material.ao",1.f);
                    }
                    m_gpu_renderer->m_modelo->meshes[i].setMaterial(material);
                }
            }
            m_material_dirty=false;
        }
    }
    void render_gpu(int width,int height) {
        sync_gpu_model();
        commons::camera* camera=m_preview_camera.get();
        if (m_view_mode==2 && m_model_entity) {
            m_scene_preview_camera.config=m_preview_camera->config;
            if (m_context->editor_camera_entity) if(auto editor_camera=m_context->registry->get<commons::camera>(*m_context->editor_camera_entity)) {
                const auto fov=m_scene_preview_camera.config.fov, near_clip=m_scene_preview_camera.config.corte_curto, far_clip=m_scene_preview_camera.config.corte_longo;
                m_scene_preview_camera.config=editor_camera->config;
                m_scene_preview_camera.config.fov=fov;m_scene_preview_camera.config.corte_curto=near_clip;m_scene_preview_camera.config.corte_longo=far_clip;
                m_scene_preview_camera.config.flag_orth=false;
                m_scene_preview_camera.skybox_asset_root=editor_camera->skybox_asset_root;
                m_scene_preview_camera.skybox_faces=editor_camera->skybox_faces;
            }
            m_scene_preview_camera.viewMatrix = m_preview_camera->viewMatrix;
            m_scene_preview_camera.position = m_preview_camera->position;
            camera = &m_scene_preview_camera;
        }
        viewport_frame frame;
        if (m_view_mode == 2 && m_model_entity) {
            frame = render_viewport(m_gpu_render_system, m_context->registry, *camera,
                width, height, *m_model_entity, m_gpu_renderer);
        } else {
            const auto transform = m_model_entity
                ? m_context->registry->get<commons::transform>(*m_model_entity) : nullptr;
            frame = render_viewport(m_gpu_render_system, m_preview_registry, *camera,
                width, height, m_gpu_renderer->my_object, {}, transform);
        }
        const bgui::vec2 size{float(frame.width), float(frame.height)};
        if (frame.texture && (m_image->get_texture().m_id != frame.texture ||
            m_image->get_texture().m_size != size))
            m_image->set_external_texture(frame.texture, size);
    }

    editor::transform_gizmo m_gizmo;
    std::shared_ptr<commons::ecs> m_preview_registry;
    std::shared_ptr<commons::camera> m_preview_camera;
    std::shared_ptr<commons::transform> m_orbit_pivot = std::make_shared<commons::transform>();
    std::shared_ptr<viewport_input_state> m_navigation_input = std::make_shared<viewport_input_state>();
    std::unique_ptr<camera_movement_system> m_navigation;
    bgui::button* m_navigation_button{};
    bool m_orbit_key_down{false};
    bgui::linear* m_mesh_controls{};
    std::vector<std::pair<bgui::element*, std::function<void(const std::filesystem::path&)>>> m_material_targets;
    std::vector<std::pair<bgui::inputbox*, std::function<std::string()>>> m_material_inputs;
    std::size_t m_material_revision{};
    std::size_t m_material_actions{};

    void record_edit(std::function<void()> undo, std::function<void()> redo) override {
        m_context->record_action(std::move(undo),std::move(redo));
    }
    void track_property_input(bgui::inputbox& input, std::function<std::string()> read) override {
        m_material_inputs.emplace_back(&input,std::move(read));
    }
    void rebuild_mesh_controls() {
        std::vector<bool> expansion;
        for (auto& [layer,elements] : m_mesh_controls->get_elements()) for (auto& element : elements)
            if (auto* group=dynamic_cast<bgui::tree*>(element.get())) expansion.push_back(group->is_expanded());
        m_material_targets.clear(); m_material_inputs.clear();
        for (auto& [layer, elements] : m_mesh_controls->get_elements())
            while (!elements.empty()) m_mesh_controls->remove(elements.back().get());
        if (auto renderer = m_source_renderer.lock()) {
            for (std::size_t i = 0; i < m_model.meshes.size(); ++i) {
                auto& group = m_mesh_controls->add<bgui::tree>("Malha " + std::to_string(i+1), .28f, i<expansion.size() && expansion[i]);
                group.add_class("model-mesh-section");
                group.children().add<bgui::button>("Selecionar malha", .26f, [this, i] {
                    if (m_extrusion_before || m_cut_before) return;
                    set_active_mesh(i);
                    m_mode = model_selection_mode::faces;
                    m_context->model_selection = static_cast<int>(m_mode);
                    m_selected_faces.clear();
                    for (std::size_t face = 0; face < m_model.meshes[i].faces.size(); ++face) m_selected_faces.insert(face);
                    m_gizmo_selection_dirty = true;
                    mark_preview_dirty();
                });
                group.children().style.layout.padding = bgui::vec4i{6,4,6,4};
                for (std::size_t g = 0; g < m_model.meshes[i].face_groups.size(); ++g) {
                    const auto& faces = m_model.meshes[i].face_groups[g];
                    group.children().add<bgui::button>(faces.name, .26f, [this, i, g] {
                        if (m_extrusion_before || m_cut_before) return;
                        if (m_active_mesh != i || !bgui::get_pressed(bgui::input_key::left_control)) m_selected_faces.clear();
                        set_active_mesh(i);
                        const auto& indices = m_model.meshes[i].face_groups[g].faces;
                        m_selected_faces.insert(indices.begin(), indices.end());
                        m_mode = model_selection_mode::faces;
                        m_context->model_selection = static_cast<int>(m_mode);
                        m_gizmo_selection_dirty = true;
                        mark_preview_dirty();
                    });
                }
                build_mesh_material_editor(*this, group.children(), m_context, renderer, i,
                    [this](auto* element, auto assign) { m_material_targets.emplace_back(element,std::move(assign)); },
                    [this](std::string message) { set_status(std::move(message)); mark_preview_dirty(); });
            }
        }
    }

    struct geometry_state {
        commons::editable_model_data model;
        std::size_t revision;
    };
    std::vector<geometry_state> m_geometry_undo;
    std::vector<geometry_state> m_geometry_redo;
    std::optional<geometry_state> m_drag_before;
    std::size_t m_geometry_revision{};
    std::size_t m_saved_geometry_revision{};
    std::size_t m_next_geometry_revision{};
    float m_frame_distance{1.f};

    geometry_state capture_geometry() const {
        return {m_model, m_geometry_revision};
    }

    void record_geometry(geometry_state before) {
        constexpr std::size_t history_limit = 32;
        if (m_geometry_undo.size() == history_limit)
            m_geometry_undo.erase(m_geometry_undo.begin());
        m_geometry_undo.push_back(std::move(before));
        m_geometry_redo.clear();
        m_geometry_revision = ++m_next_geometry_revision;
        m_dirty = true;
        m_material_revision = std::numeric_limits<std::size_t>::max();
    }

    void restore_geometry(bool redo) {
        auto& source = redo ? m_geometry_redo : m_geometry_undo;
        auto& destination = redo ? m_geometry_undo : m_geometry_redo;
        if (source.empty() || m_gizmo.is_dragging() || m_extrusion_before || m_cut_before) return;
        auto state = std::move(source.back());
        source.pop_back();
        for (std::size_t i = 0; i < m_model.meshes.size(); ++i) {
            auto& current = m_model.meshes[i];
            auto& previous = state.model.meshes[i];
            current.vertices.swap(previous.vertices);
            current.faces.swap(previous.faces);
            current.face_groups.swap(previous.face_groups);
        }
        std::swap(state.revision, m_geometry_revision);
        destination.push_back(std::move(state));
        m_dirty = m_geometry_revision != m_saved_geometry_revision;
        clear_element_selection();
        m_geometry_dirty = m_projection_dirty = m_gizmo_selection_dirty = true;
        m_material_revision = std::numeric_limits<std::size_t>::max();
        update_mesh_label();
        set_status(redo ? "Edição de geometria refeita." : "Edição de geometria desfeita.");
        mark_preview_dirty();
    }

    commons::transform m_gizmo_transform;
    std::vector<std::pair<std::size_t, std::size_t>> m_gizmo_vertices;
    bgui::vec2i m_gizmo_last_mouse{};
    bool m_gizmo_left_down{false};
    bool m_gizmo_selection_dirty{true};
    bool m_frame_preview{true};
    bgui::vec3 m_model_center{};
    model_selection_mode m_last_edit_mode{model_selection_mode::vertices};
    bool m_tab_down{}, m_extrude_down{}, m_cut_down{};
    bool m_picking_cut{};
    std::optional<geometry_state> m_cut_before;
    std::vector<mesh_edge> m_cut_rails;
    bgui::vec2i m_cut_start{};
    bgui::vec2 m_cut_screen_edge{};

    void draw_cut_slide(const bgui::vec4i& rect) const {
        if (m_projection_dirty) return;
        const auto size = m_image->get_texture().m_size;
        if (size.x <= 0 || size.y <= 0) return;
        const auto first_new = m_cut_before->model.meshes[m_active_mesh].vertices.size();
        const auto& points = m_projected_depths[m_active_mesh];
        auto& draw = bgui::get_draw_list();
        const auto color = draw.get_color();
        const auto clip = draw.get_clip_rect();
        const bool clipped = draw.has_clip_rect();
        draw.set_clip_rect(clipped ? bgui::intersect_rect(clip, rect) : rect);
        draw.set_color({1.f, .65f, .1f, 1.f});
        const auto screen = [&](const bgui::vec3& p) {
            return bgui::vec2{rect.x + p.x * rect.z / size.x, rect.y + p.y * rect.w / size.y};
        };
        for (const auto& face : m_model.meshes[m_active_mesh].faces) {
            for (std::size_t i = 0; i < face.size(); ++i) {
                const auto a = face[i], b = face[(i + 1) % face.size()];
                if (a < first_new || b < first_new || a > b) continue;
                if (std::abs(points[a].z) <= 1.f && std::abs(points[b].z) <= 1.f)
                    draw.add_line(screen(points[a]), screen(points[b]), 2.f);
            }
        }
        draw.set_color(color);
        if (clipped) draw.set_clip_rect(clip);
        else draw.clear_clip_rect();
    }

    void update_cut_slide(bgui::vec2i mouse, bool confirm, bool center, bool cancel) {
        auto& mesh = m_model.meshes[m_active_mesh];
        const auto& original = m_cut_before->model.meshes[m_active_mesh];
        if (cancel) {
            mesh = std::move(m_cut_before->model.meshes[m_active_mesh]);
            m_dirty = m_geometry_revision != m_saved_geometry_revision;
            set_status("Corte cancelado.");
        } else {
            const auto delta = mouse - m_cut_start;
            const auto edge = m_cut_screen_edge;
            const float length_squared = edge.x * edge.x + edge.y * edge.y;
            const float fraction = center ? .5f : std::clamp(
                .5f + (delta.x * edge.x + delta.y * edge.y) / length_squared, .001f, .999f);
            slide_loop_cut(mesh, original.vertices.size(), m_cut_rails, fraction);
            if (confirm || center) {
                record_geometry(std::move(*m_cut_before));
                set_status("Loop Cut aplicado. Salve o arquivo .bmodel.");
            }
        }
        if (confirm || center || cancel) {
            m_cut_before.reset();
            m_cut_rails.clear();
        }
        m_geometry_dirty = m_projection_dirty = m_gizmo_selection_dirty = true;
        update_mesh_label();
        mark_preview_dirty();
    }
    std::optional<geometry_state> m_extrusion_before;
    int m_extrusion_start_y{};
    bool m_extrusion_changed{};

    void update_extrusion(int y, bool confirm, bool cancel) {
        if (cancel) {
            m_model.meshes[m_active_mesh] = std::move(m_extrusion_before->model.meshes[m_active_mesh]);
            m_extrusion_before.reset();
            m_dirty = m_geometry_revision != m_saved_geometry_revision;
            set_status("Extrusão cancelada.");
        } else {
            const float distance = (m_extrusion_start_y - y) * m_frame_distance /
                std::max(1, m_image->processed_height());
            if (std::abs(distance) < 1e-6f) {
                if (!m_extrusion_changed) return;
                m_model.meshes[m_active_mesh] = m_extrusion_before->model.meshes[m_active_mesh];
                m_extrusion_changed = false;
            } else {
                auto candidate = m_extrusion_before->model.meshes[m_active_mesh];
                std::string error;
                if (!extrude_faces(candidate, m_selected_faces, distance, error)) {
                    set_status(error);
                    return;
                }
                m_model.meshes[m_active_mesh] = std::move(candidate);
                m_extrusion_changed = true;
            }
        }
        if (confirm && m_extrusion_before && m_extrusion_changed) {
            const auto count = m_selected_faces.size();
            const auto end = m_extrusion_before->model.meshes[m_active_mesh].faces.size();
            m_selected_faces.clear();
            for (auto face = end - count; face < end; ++face) m_selected_faces.insert(face);
            record_geometry(std::move(*m_extrusion_before));
            m_extrusion_before.reset();
            set_status("Extrusão aplicada. Salve o arquivo .bmodel.");
        }
        m_geometry_dirty = m_projection_dirty = m_gizmo_selection_dirty = true;
        update_mesh_label();
        mark_preview_dirty();
    }

    static bool extend_selection() {
        return bgui::get_pressed(bgui::input_key::left_shift) ||
            bgui::get_pressed(bgui::input_key::right_shift) ||
            bgui::get_pressed(bgui::input_key::left_control) ||
            bgui::get_pressed(bgui::input_key::right_control);
    }

    void set_selection_mode(model_selection_mode mode) {
        if (mode != model_selection_mode::object) m_last_edit_mode = mode;
        m_context->model_selection = static_cast<int>(mode);
        m_mode = mode;
        m_selection_start.reset();
        clear_element_selection();
        m_gizmo_selection_dirty = true;
        refresh_mode_buttons();
        mark_preview_dirty();
    }

    void draw_gizmo() {
        const bool editable = is_native_model(m_source_path);
        const auto mouse = bgui::get_mouse_position();
        const bool blocked = !m_context || m_context->is_playing || viewport_input_blocked_by_modal();
        const bool down = !blocked && bgui::get_pressed(bgui::input_key::mouse_left);
        const bool just_pressed = down && !m_gizmo_left_down;
        const bool just_released = !down && m_gizmo_left_down;
        const bool delete_key = bgui::get_pressed(bgui::input_key::delete_key);
        const bool delete_pressed = delete_key && !m_delete_down;
        m_delete_down = delete_key;
        const auto delta = mouse - m_gizmo_last_mouse;
        m_gizmo_last_mouse = mouse;
        m_gizmo_left_down = down;
        const auto rect = m_image->processed_rect();
        const bool over = !blocked && !bgui::get_pressed(bgui::input_key::mouse_right) &&
            bgui::get_mouse_target() == m_pick_overlay &&
            !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus());
        const bool tab = bgui::get_pressed(bgui::input_key::tab);
        const bool extrude_key = bgui::get_pressed(bgui::input_key::e);
        const bool control = bgui::get_pressed(bgui::input_key::left_control) ||
            bgui::get_pressed(bgui::input_key::right_control);
        const bool cut = control && bgui::get_pressed(bgui::input_key::r);
        if (m_cut_before) {
            draw_cut_slide(rect);
            const bool center = bgui::get_pressed(bgui::input_key::mouse_right);
            const bool cancel = blocked || bgui::get_pressed(bgui::input_key::escape);
            const bool confirm = just_pressed && over;
            m_tab_down = tab;
            m_extrude_down = extrude_key;
            m_cut_down = cut;
            if (delta.x || delta.y || confirm || center || cancel)
                update_cut_slide(mouse, confirm, center, cancel);
            return;
        }
        const bool cancel_selection = blocked || bgui::get_pressed(bgui::input_key::escape) ||
            bgui::get_pressed(bgui::input_key::mouse_right);
        if (cancel_selection) m_selection_start.reset();
        if (over && editable && !m_gizmo.is_dragging() && !m_extrusion_before && !m_selection_start) {
            if (delete_pressed && !m_picking_cut) delete_selection();
            if (tab && !m_tab_down) {
                m_picking_cut = false;
                set_selection_mode(m_mode == model_selection_mode::object
                    ? m_last_edit_mode : model_selection_mode::object);
            }
            if (extrude_key && !m_extrude_down && !control && !m_picking_cut &&
                m_mode == model_selection_mode::faces && !m_selected_faces.empty()) {
                m_extrusion_before = capture_geometry();
                m_extrusion_start_y = mouse.y;
                m_extrusion_changed = false;
                m_dirty = true;
                set_status("Mova o mouse para extrudar. Clique para confirmar ou Esc para cancelar.");
            }
            if (cut && !m_cut_down) start_loop_cut(true);
        }
        m_tab_down = tab;
        m_extrude_down = extrude_key;
        m_cut_down = cut;
        if (m_extrusion_before) {
            const bool cancel = blocked || bgui::get_pressed(bgui::input_key::escape) ||
                bgui::get_pressed(bgui::input_key::mouse_right);
            if (delta.y != 0 || just_pressed || cancel)
                update_extrusion(mouse.y, just_pressed && over, cancel);
            return;
        }
        if (m_picking_cut && (blocked || bgui::get_pressed(bgui::input_key::escape) ||
                bgui::get_pressed(bgui::input_key::mouse_right))) {
            m_picking_cut = false;
            set_status("Corte cancelado.");
        }
        if (m_picking_cut && just_pressed && over) {
            select_at(mouse);
            if (m_selected_edges.size() == 1) {
                m_picking_cut = false;
                loop_cut();
            }
            return;
        }
        if (m_gizmo_selection_dirty && !m_gizmo.is_dragging()) {
            m_gizmo_vertices.clear();
            bgui::vec3 pivot{};
            for (std::size_t mesh = 0; mesh < m_model.meshes.size(); ++mesh) {
                std::set<std::size_t> selected;
                if (m_mode == model_selection_mode::object && m_object_selected) {
                    for (std::size_t v = 0; v < m_model.meshes[mesh].vertices.size(); ++v) {
                        m_gizmo_vertices.emplace_back(mesh,v);pivot+=world_point(m_model.meshes[mesh].vertices[v].position);
                    }
                    continue;
                } else if (mesh == m_active_mesh) {
                    if (m_mode == model_selection_mode::faces)
                        for (auto face : m_selected_faces)
                            if (face < m_model.meshes[mesh].faces.size())
                                selected.insert(m_model.meshes[mesh].faces[face].begin(), m_model.meshes[mesh].faces[face].end());
                    if (m_mode == model_selection_mode::vertices) selected = m_selected_vertices;
                    if (m_mode == model_selection_mode::edges)
                        for (const auto& [first, second] : m_selected_edges) {
                            selected.insert(first);
                            selected.insert(second);
                        }
                }
                for (auto vertex : selected) if (vertex < m_model.meshes[mesh].vertices.size()) {
                    m_gizmo_vertices.emplace_back(mesh, vertex);
                    pivot += world_point(m_model.meshes[mesh].vertices[vertex].position);
                }
            }
            if (!m_gizmo_vertices.empty()) pivot = pivot / static_cast<float>(m_gizmo_vertices.size());
            m_orbit_pivot->set_position(m_mode != model_selection_mode::object &&
                !m_gizmo_vertices.empty() ? pivot : m_model_center);
            m_gizmo_transform.set_position(pivot);
            m_gizmo_transform.set_rotation({});
            m_gizmo_transform.set_scale({1.f, 1.f, 1.f});
            m_gizmo_selection_dirty = false;
        }
        const auto before = m_gizmo_transform.get_model_matrix();
        auto* target = !editable || !m_world_to_model || m_picking_cut || m_selection_start || m_gizmo_vertices.empty() ? nullptr : &m_gizmo_transform;
        const bool was_dragging = m_gizmo.is_dragging();
        m_gizmo.update(*m_preview_camera, target, rect, {rect}, mouse, delta,
            over, just_pressed && over, down && (over || m_gizmo.is_dragging()));
        const auto after = m_gizmo_transform.get_model_matrix();
        if (target && before != after) {
            if (!m_drag_before) m_drag_before = capture_geometry();
            const auto change = *m_world_to_model * after * glm::inverse(before) * m_model_world;
            for (auto [mesh, vertex] : m_gizmo_vertices) {
                auto& point = m_model.meshes[mesh].vertices[vertex].position;
                const auto next = change * glm::vec4(point.x, point.y, point.z, 1.f);
                point = {next.x, next.y, next.z};
            }
            for (auto& mesh : m_model.meshes) mesh.recalculate_normals();
            m_dirty = true;m_geometry_dirty=true;m_projection_dirty=true;
            update_mesh_label();
            mark_preview_dirty();
        }
        if (!m_gizmo.is_dragging() && just_pressed && over && !cancel_selection) {
            if (m_mode == model_selection_mode::object) select_at(mouse);
            else {
                m_selection_start = mouse;
                m_selection_extend = extend_selection();
            }
        }
        if (m_selection_start) {
            const auto start = *m_selection_start;
            const bool box = std::abs(mouse.x - start.x) >= 4 || std::abs(mouse.y - start.y) >= 4;
            if (just_released) {
                m_selection_start.reset();
                if (box) select_rectangle(start, mouse, m_selection_extend);
                else if (over) select_at(start, m_selection_extend);
            } else if (box) {
                draw_selection_rectangle(start, mouse, rect);
            }
        }
        if (!m_gizmo.is_dragging() && m_drag_before) {
            record_geometry(std::move(*m_drag_before));
            m_drag_before.reset();
        }
        if (was_dragging && !m_gizmo.is_dragging()) m_gizmo_selection_dirty = true;
        if(m_active_mesh<m_projected_vertices.size()) {
            auto& draw=bgui::get_draw_list();const auto previous_color=draw.get_color();
            const auto previous_clip=draw.get_clip_rect();const bool clipped=draw.has_clip_rect();
            draw.set_clip_rect(clipped?bgui::intersect_rect(previous_clip,rect):rect);draw.set_color({1.f,.45f,.12f,1.f});
            const auto size=m_image->get_texture().m_size;
            const auto screen=[&](std::size_t v){ const auto p=m_projected_vertices[m_active_mesh][v];return bgui::vec2{rect.x+p.x*rect.z/std::max(1.f,size.x),rect.y+p.y*rect.w/std::max(1.f,size.y)}; };
            if(m_mode==model_selection_mode::faces) for(auto face:m_selected_faces) if(face<m_model.meshes[m_active_mesh].faces.size()) {
                const auto& indices=m_model.meshes[m_active_mesh].faces[face];
                for(std::size_t i=0;i<indices.size();++i)draw.add_line(screen(indices[i]),screen(indices[(i+1)%indices.size()]),2.f);
            }
            if (m_mode == model_selection_mode::edges)
                for (const auto& [first, second] : m_selected_edges)
                    draw.add_line(screen(first), screen(second), 3.f);
            if (m_mode == model_selection_mode::vertices && !m_projection_dirty) {
                const auto& points = m_projected_depths[m_active_mesh];
                for (std::size_t vertex = 0; vertex < points.size(); ++vertex) {
                    if (std::abs(points[vertex].z) > 1.f) continue;
                    const bool selected = m_selected_vertices.contains(vertex);
                    const auto p = screen(vertex);
                    const auto radius = selected ? bgui::vec2{3.f, 3.f} : bgui::vec2{2.f, 2.f};
                    draw.set_color(selected ? bgui::color{1.f, .45f, .12f, 1.f}
                        : bgui::color{.75f, .75f, .75f, 1.f});
                    draw.add_rect_filled(p - radius, p + radius);
                }
            }
            draw.set_color(previous_color);if(clipped)draw.set_clip_rect(previous_clip);else draw.clear_clip_rect();
        }
    }


    void sync_selected_entity() {
        if (!m_context || !m_context->registry)
            return;

        const auto selected = m_context->selected_entity;
        std::shared_ptr<commons::renderer> renderer;
        std::filesystem::path selected_path;
        if (selected && m_context->registry->is_alive(*selected)) {
            renderer = m_context->registry->get<commons::renderer>(*selected);
            if (renderer && renderer->m_modelo)
                selected_path = renderer->m_modelo->get_source_path();
        }

        if (selected == m_model_entity && selected_path == m_source_path &&
            renderer == m_source_renderer.lock() &&
            (renderer || m_model.meshes.empty()))
            return;

        if (m_dirty) {
            set_status(
                "Salve as alterações do modelo atual antes de trocar a entidade."
            );
            return;
        }

        m_picking_cut = false;
        m_selection_start.reset();
        m_geometry_undo.clear();
        m_geometry_redo.clear();
        m_drag_before.reset();
        m_saved_geometry_revision = m_geometry_revision = 0;
        m_gizmo = editor::transform_gizmo{};
        m_geometry_dirty=true;m_material_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        m_frame_preview = true;
        m_source_renderer = renderer;
        m_model_entity.reset();
        m_source_path.clear();
        m_model.meshes.clear();
        rebuild_mesh_controls();
        m_active_mesh = 0;
        clear_element_selection();
        m_object_selected = false;
        m_dirty = false;
        update_mesh_label();
        if (m_selection_label) m_selection_label->set_buffer("Nenhum objeto selecionado");
        if (m_source_label) m_source_label->set_buffer("Selecione um objeto na cena ou na Hierarquia.");
        mark_preview_dirty();

        if (!selected || !m_context->registry->is_alive(*selected)) {
            m_model_entity = selected;
            set_status("Selecione uma entidade com modelo 3D na cena ou na Hierarquia.");
            return;
        }
        m_model_entity = selected;
        const auto name = m_context->entity_names.find(*selected);
        m_selection_label->set_buffer(name != m_context->entity_names.end()
            ? name->second : "Objeto " + std::to_string(*selected));
        if (!renderer || !renderer->m_modelo) {
            set_status("A entidade selecionada não possui um modelo 3D editável.");
            return;
        }

        m_source_path = selected_path;
        m_source_label->set_buffer(selected_path.empty() ? "Geometria do objeto em cena"
            : selected_path.filename().string());
        commons::editable_model_data imported;
        std::string error;
        if (is_native_model(selected_path)) {
            if (!commons::load_editable_model(selected_path, imported, error)) {
                set_status(error);
                return;
            }
        } else {
            for (const auto& source : renderer->m_modelo->meshes) {
                const auto& indices = source.indices();
                if (indices.size() % 3 != 0) {
                    set_status("A malha contém índices incompletos.");
                    return;
                }
                commons::editable_mesh_data mesh;
                mesh.vertices = source.vertices();
                for (std::size_t i = 0; i < indices.size(); i += 3)
                    mesh.faces.push_back({indices[i], indices[i + 1], indices[i + 2]});
                imported.meshes.push_back(std::move(mesh));
            }
            if (!imported.validate(error)) {
                set_status(error);
                return;
            }
        }
        m_model = std::move(imported);
        m_geometry_dirty=true;m_material_dirty=true;m_projection_dirty=true;
        rebuild_mesh_controls();
        m_active_mesh = 0;
        clear_element_selection();
        m_object_selected = true;
        update_mesh_label();
        set_status(is_native_model(selected_path)
            ? "Modelo do objeto selecionado. Escolha um modo de edição."
            : "Prévia da geometria da cena. Importe como .bmodel para editar e salvar.");
        mark_preview_dirty();
    }

    void add_mode_button(
        bgui::linear& parent,
        const char* label,
        const model_selection_mode mode
    ) {
        auto& button = parent.add<bgui::button>(label, 0.27f, [this, mode]() {
            if (m_extrusion_before || m_cut_before || m_gizmo.is_dragging()) return;
            m_picking_cut = false;
            set_selection_mode(mode);
        });
        button.style.layout.require_mode(bgui::mode::stretch, bgui::mode::pixel);
        button.style.layout.require_height(bgui::mode::pixel, 26.f);
        button.style.layout.margin = bgui::vec4i{2};
        m_mode_buttons.push_back(&button);
    }

    void refresh_mode_buttons() {
        for (std::size_t index = 0; index < m_mode_buttons.size(); ++index) {
            auto& background = m_mode_buttons[index]->style.visual.background;
            const bool active = static_cast<std::size_t>(m_mode) == index;
            background.normal = active
                ? bgui::color{0.17f, 0.36f, 0.58f, 1.f}
                : bgui::color{0.f, 0.f, 0.f, 0.f};
            background.hover = bgui::color{0.17f, 0.17f, 0.17f, 1.f};
            m_mode_buttons[index]->mark_style_dirty();
        }
    }

    void set_status(std::string message) {
        if (m_status)
            m_status->set_buffer(std::move(message));
    }

    void update_mesh_label() {
        if (!m_mesh_label)
            return;
        if (m_model.meshes.empty()) {
            m_mesh_label->set_buffer("Nenhum modelo");
            return;
        }
        m_mesh_label->set_buffer(
            "Malha " + std::to_string(m_active_mesh + 1) + "/" +
            std::to_string(m_model.meshes.size()) + (m_dirty ? " *" : "")
        );
    }

    void mark_preview_dirty() {
        m_preview_dirty = true;
        // Selection and geometry invalidation are tracked separately from camera/material changes.
        if (m_image)
            m_image->mark_render_dirty();
    }

    static int pixel_index(
        const int x,
        const int y,
        const int width
    ) {
        return y * width + x;
    }

    static void set_pixel(
        bgui::texture& texture,
        const int x,
        const int y,
        const bgui::color& color
    ) {
        if (x < 0 || y < 0 || x >= texture.m_size.x || y >= texture.m_size.y)
            return;
        const auto index = static_cast<std::size_t>(
            pixel_index(x, y, texture.m_size.x) * 4
        );
        for (int channel = 0; channel < 3; ++channel)
            texture.m_buffer[index + channel] = static_cast<unsigned char>(
                std::clamp(color[channel], 0.f, 1.f) * 255.f
            );
        texture.m_buffer[index + 3] = 255;
    }

    static float edge_function(
        const bgui::vec2& a,
        const bgui::vec2& b,
        const float x,
        const float y
    ) {
        return (b.x - a.x) * (y - a.y) -
            (b.y - a.y) * (x - a.x);
    }

    void render_model_preview(bgui::texture& texture) {
        const int width = texture.m_size.x;
        const int height = texture.m_size.y;

        m_projected_vertices.resize(m_model.meshes.size());
        m_projected_depths.resize(m_model.meshes.size());
        for (std::size_t mesh=0;mesh<m_model.meshes.size();++mesh) {
            m_projected_vertices[mesh].clear();
            m_projected_depths[mesh].clear();
            m_projected_vertices[mesh].reserve(m_model.meshes[mesh].vertices.size());
            m_projected_depths[mesh].reserve(m_model.meshes[mesh].vertices.size());
        }
        if (m_model.meshes.empty())
            return;

        if (m_frame_preview) {
            const auto origin = world_point({});
            bgui::vec3 low = origin, high = origin;
            bool first = true;
            for (const auto& mesh : m_model.meshes) for (const auto& vertex : mesh.vertices) {
                const auto point = world_point(vertex.position);
                if (first) {
                    low = high = point;
                    first = false;
                }
                for (int axis = 0; axis < 3; ++axis) {
                    low[axis] = std::min(low[axis], point[axis]);
                    high[axis] = std::max(high[axis], point[axis]);
                }
            }
            const auto center = (low+high)*.5f;
            const float radius = std::max(.01f,(high-low).length()*.5f);
            const float aspect = static_cast<float>(width)/height;
            const float distance = radius / (std::sin(glm::radians(m_preview_camera->config.fov)*.5f)*std::min(1.f,aspect)) * 1.2f;
            m_frame_distance = distance;
            m_preview_camera->config.corte_longo = std::max(300.f,distance+radius*4.f);
            m_preview_camera->config.corte_curto = std::max(1e-6f,distance*1e-4f);
            m_navigation->set_orbit_distance_limits(distance * .005f, distance * 100.f);


            const float yaw = .65f - 1.57079632679f, pitch = -.4f;
            const bgui::vec3 forward{std::cos(pitch)*std::cos(yaw),std::sin(pitch),std::cos(pitch)*std::sin(yaw)};
            auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
            eye->set_position(center-forward*distance);
            eye->set_rotation({glm::degrees(pitch),glm::degrees(yaw),0.f});
            m_model_center = center;
            m_gizmo_selection_dirty = true;
            m_orbit_pivot->set_position(center);
            m_navigation->set_orbital(false);
            if (m_context->orbital_view) m_navigation->focus_orbit_target(m_orbit_pivot,distance);
            m_frame_preview = false;
        }
        update_viewport_projection(*m_preview_camera, width, height);
        const auto matrix = m_preview_camera->projMatriz * m_preview_camera->viewMatrix * m_model_world;

        for (std::size_t mesh=0; mesh<m_model.meshes.size(); ++mesh) {
            for (const auto& vertex : m_model.meshes[mesh].vertices) {
                const auto p=vertex.position;
                const auto clip=matrix*glm::vec4(p.x,p.y,p.z,1.f);
                if (clip.w <= 1e-5f) {
                    m_projected_depths[mesh].push_back({0.f,0.f,2.f});

                    m_projected_vertices[mesh].push_back({-100000.f,-100000.f});
                } else {
                    const auto ndc=glm::vec3(clip)/clip.w;
                    const bgui::vec2 point{std::clamp((ndc.x*.5f+.5f)*width,-100000.f,100000.f),
                        std::clamp((.5f-ndc.y*.5f)*height,-100000.f,100000.f)};
                    m_projected_depths[mesh].push_back({point.x,point.y,ndc.z});

                    m_projected_vertices[mesh].push_back(point);
                }
            }
        }

        ++m_projection_updates;
    }

    static void draw_line(
        bgui::texture& texture,
        bgui::vec2 first,
        bgui::vec2 second,
        const bgui::color& color
    ) {
        const auto delta = second-first;
        float enter=0.f, leave=1.f;
        const auto clip = [&](float p, float q) {
            if (std::abs(p)<1e-8f) return q>=0.f;
            const float r=q/p;
            if (p<0.f) enter=std::max(enter,r); else leave=std::min(leave,r);
            return enter<=leave;
        };
        if (!clip(-delta.x,first.x) || !clip(delta.x,texture.m_size.x-1-first.x) ||
            !clip(-delta.y,first.y) || !clip(delta.y,texture.m_size.y-1-first.y)) return;
        second=first+delta*leave; first=first+delta*enter;
        int x0 = static_cast<int>(std::lround(first.x));
        int y0 = static_cast<int>(std::lround(first.y));
        const int x1 = static_cast<int>(std::lround(second.x));
        const int y1 = static_cast<int>(std::lround(second.y));
        const int dx = std::abs(x1 - x0);
        const int sx = x0 < x1 ? 1 : -1;
        const int dy = -std::abs(y1 - y0);
        const int sy = y0 < y1 ? 1 : -1;
        int error = dx + dy;
        while (true) {
            set_pixel(texture, x0, y0, color);
            if (x0 == x1 && y0 == y1)
                break;
            const int twice = 2 * error;
            if (twice >= dy) {
                error += dy;
                x0 += sx;
            }
            if (twice <= dx) {
                error += dx;
                y0 += sy;
            }
        }
    }

    static void draw_square(
        bgui::texture& texture,
        const int x,
        const int y,
        const int radius,
        const bgui::color& color
    ) {
        for (int py = y - radius; py <= y + radius; ++py)
            for (int px = x - radius; px <= x + radius; ++px)
                set_pixel(texture, px, py, color);
    }

    void refresh_preview() {
        const auto start=std::chrono::steady_clock::now();
        m_preview_dirty=false;
        if(!m_image)return;
        const int width=std::clamp(m_image->processed_width(),240,1280),height=std::clamp(m_image->processed_height(),180,900);
        bgui::texture texture;texture.m_size={float(width),float(height)};
        if(m_projection_dirty || m_frame_preview) {
            render_model_preview(texture);m_projection_dirty=false;
        }
        if(!m_model.meshes.empty() && gpu_available()) render_gpu(width,height);
        else {
            texture.m_path="model-editor-preview";texture.m_revision=++m_preview_revision;
            texture.m_generate_mipmap=false;texture.m_has_alpha=true;
            texture.m_buffer.resize(static_cast<std::size_t>(width)*height*4);
            for(std::size_t i=0;i<texture.m_buffer.size();i+=4) {
                texture.m_buffer[i]=14;texture.m_buffer[i+1]=15;texture.m_buffer[i+2]=18;texture.m_buffer[i+3]=255;
            }
            // Headless fallback: project wire edges, without rasterizing triangle fills.
            for(std::size_t mesh=0;mesh<m_model.meshes.size();++mesh)for(const auto& face:m_model.meshes[mesh].faces)
                for(std::size_t i=0;i<face.size();++i) {
                    const auto a=face[i],b=face[(i+1)%face.size()];
                    if(m_projected_depths[mesh][a].z>=-1.f && m_projected_depths[mesh][a].z<=1.f && m_projected_depths[mesh][b].z>=-1.f && m_projected_depths[mesh][b].z<=1.f)
                        draw_line(texture,m_projected_vertices[mesh][a],m_projected_vertices[mesh][b],{.65f,.65f,.65f,1.f});
                }
            m_image->set_texture(texture);
        }
        m_last_preview_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }

    void clear_element_selection() {
        m_selected_faces.clear();
        m_selected_vertices.clear();
        m_selected_edges.clear();
    }

    void set_active_mesh(std::size_t mesh) {
        if (mesh != m_active_mesh) clear_element_selection();
        m_active_mesh = mesh;
    }

    void delete_selection() {
        if (m_mode == model_selection_mode::object || m_active_mesh >= m_model.meshes.size())
            return;

        const std::set<std::size_t> empty_indices;
        const std::set<mesh_edge> empty_edges;
        const auto& faces = m_mode == model_selection_mode::faces ? m_selected_faces : empty_indices;
        const auto& vertices = m_mode == model_selection_mode::vertices ? m_selected_vertices : empty_indices;
        const auto& edges = m_mode == model_selection_mode::edges ? m_selected_edges : empty_edges;
        if (faces.empty() && vertices.empty() && edges.empty()) return;

        auto before = capture_geometry();
        std::string error;
        if (!delete_mesh_elements(m_model.meshes[m_active_mesh], faces, vertices, edges, error)) {
            if (!error.empty()) set_status(error);
            return;
        }
        clear_element_selection();
        record_geometry(std::move(before));
        m_geometry_dirty = m_projection_dirty = m_gizmo_selection_dirty = true;
        update_mesh_label();
        set_status("Seleção apagada. Salve o arquivo .bmodel.");
        mark_preview_dirty();
    }

    static void draw_selection_rectangle(bgui::vec2i start, bgui::vec2i end, const bgui::vec4i& rect) {
        auto& draw = bgui::get_draw_list();
        const auto color = draw.get_color();
        const auto clip = draw.get_clip_rect();
        const bool clipped = draw.has_clip_rect();
        draw.set_clip_rect(clipped ? bgui::intersect_rect(clip, rect) : rect);
        const bgui::vec2 low{float(std::min(start.x, end.x)), float(std::min(start.y, end.y))};
        const bgui::vec2 high{float(std::max(start.x, end.x)), float(std::max(start.y, end.y))};
        draw.set_color({.25f, .6f, 1.f, .18f});
        draw.add_rect_filled(low, high);
        draw.set_color({.35f, .7f, 1.f, 1.f});
        draw.add_rect(low, high);
        draw.set_color(color);
        if (clipped) draw.set_clip_rect(clip);
        else draw.clear_clip_rect();
    }

    void select_rectangle(bgui::vec2i start, bgui::vec2i end, bool extend) {
        if (m_projection_dirty || m_model.meshes.empty()) return;
        const auto rect = m_image->processed_rect();
        const auto size = m_image->get_texture().m_size;
        if (rect.z <= 0 || rect.w <= 0 || size.x <= 0.f || size.y <= 0.f) return;
        const auto pixel = [&](bgui::vec2i point) {
            return bgui::vec2{
                std::clamp(point.x - rect.x, 0, rect.z) * size.x / rect.z,
                std::clamp(point.y - rect.y, 0, rect.w) * size.y / rect.w
            };
        };
        const auto first = pixel(start), second = pixel(end);
        const auto add_matches = [&](std::size_t mesh_index) {
            const auto inside = [&](std::size_t vertex) {
                const auto& p = m_projected_depths[mesh_index][vertex];
                return std::abs(p.z) <= 1.f &&
                    p.x >= std::min(first.x, second.x) && p.x <= std::max(first.x, second.x) &&
                    p.y >= std::min(first.y, second.y) && p.y <= std::max(first.y, second.y);
            };
            std::set<std::size_t> indices;
            std::set<mesh_edge> edges;
            const auto& mesh = m_model.meshes[mesh_index];
            if (m_mode == model_selection_mode::vertices) {
                for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex)
                    if (inside(vertex)) indices.insert(vertex);
            } else if (m_mode == model_selection_mode::faces) {
                for (std::size_t face = 0; face < mesh.faces.size(); ++face)
                    if (std::ranges::all_of(mesh.faces[face], inside)) indices.insert(face);
            } else {
                for (const auto& face : mesh.faces)
                    for (std::size_t i = 0; i < face.size(); ++i) {
                        const auto a = face[i], b = face[(i + 1) % face.size()];
                        if (inside(a) && inside(b)) edges.insert(ordered_mesh_edge(a, b));
                    }
            }
            if (indices.empty() && edges.empty()) return false;
            set_active_mesh(mesh_index);
            auto& selected = m_mode == model_selection_mode::faces ? m_selected_faces : m_selected_vertices;
            selected.insert(indices.begin(), indices.end());
            m_selected_edges.insert(edges.begin(), edges.end());
            return true;
        };

        if (!extend) clear_element_selection();
        if (!add_matches(m_active_mesh) && !extend) {
            for (std::size_t mesh = 0; mesh < m_model.meshes.size(); ++mesh)
                if (mesh != m_active_mesh && add_matches(mesh)) break;
        }
        const auto count = m_mode == model_selection_mode::faces ? m_selected_faces.size()
            : m_mode == model_selection_mode::vertices ? m_selected_vertices.size() : m_selected_edges.size();
        set_status("Elementos selecionados: " + std::to_string(count));
        m_gizmo_selection_dirty = true;
        update_mesh_label();
        mark_preview_dirty();
    }

    void select_at(const bgui::vec2i& position, bool extend = false) {
        if (m_model.meshes.empty() || !m_image || m_projection_dirty)
            return;
        const auto image_rect = m_image->processed_rect();
        const float x = static_cast<float>(position.x - image_rect.x);
        const float y = static_cast<float>(position.y - image_rect.y);
        const auto width = static_cast<int>(
            m_image->get_texture().m_size.x
        );
        const auto height = static_cast<int>(
            m_image->get_texture().m_size.y
        );
        if (width <= 0 || height <= 0)
            return;
        const int pixel_x = std::clamp(
            static_cast<int>(x * width / std::max(1, image_rect.z)),
            0,
            width - 1
        );
        const int pixel_y = std::clamp(
            static_cast<int>(y * height / std::max(1, image_rect.w)),
            0,
            height - 1
        );

        if (m_mode == model_selection_mode::object) {
            m_object_selected = true;
            set_status("Objeto selecionado.");
        } else if (m_mode == model_selection_mode::faces) {
            std::pair<std::size_t,std::size_t> selected{std::numeric_limits<std::size_t>::max(),0};
            float nearest=std::numeric_limits<float>::max();
            for(std::size_t mesh=0;mesh<m_model.meshes.size();++mesh)for(std::size_t face=0;face<m_model.meshes[mesh].faces.size();++face) {
                const auto& corners=m_model.meshes[mesh].faces[face];
                for(std::size_t i=1;i+1<corners.size();++i) {
                    const auto a=m_projected_depths[mesh][corners[0]],b=m_projected_depths[mesh][corners[i]],c=m_projected_depths[mesh][corners[i+1]];
                    if(a.z < -1.f || b.z < -1.f || c.z < -1.f || a.z>1.f || b.z>1.f || c.z>1.f)continue;
                    const bgui::vec2 pa{a.x,a.y},pb{b.x,b.y},pc{c.x,c.y};
                    const float area=edge_function(pa,pb,c.x,c.y);if(std::abs(area)<1e-8f)continue;
                    const float u=edge_function(pb,pc,float(pixel_x),float(pixel_y))/area;
                    const float v=edge_function(pc,pa,float(pixel_x),float(pixel_y))/area,w=1.f-u-v;
                    const float depth=u*a.z+v*b.z+w*c.z;
                    if(u>=0.f && v>=0.f && w>=0.f && depth<nearest) { nearest=depth;selected={mesh,face}; }
                }
            }
            if(selected.first==std::numeric_limits<std::size_t>::max())return;
            set_active_mesh(selected.first);
            if (!extend) m_selected_faces.clear();
            if (!m_selected_faces.erase(selected.second)) m_selected_faces.insert(selected.second);
            set_status("Face " + std::to_string(selected.second + 1) +
                " selecionada.");
        } else if (m_mode == model_selection_mode::vertices) {
            if (!select_vertex(x * width / std::max(1, image_rect.z),
                    y * height / std::max(1, image_rect.w), extend))
                return;
        } else {
            if (!select_edge(x * width / std::max(1, image_rect.z),
                    y * height / std::max(1, image_rect.w), extend))
                return;
        }
        m_gizmo_selection_dirty=true;
        update_mesh_label();
        mark_preview_dirty();
    }

    bool select_vertex(const float x, const float y, bool extend) {
        float best_distance = 8.f;
        std::optional<std::pair<std::size_t, std::size_t>> best;
        for (std::size_t mesh = 0; mesh < m_projected_vertices.size(); ++mesh) {
            for (std::size_t vertex = 0;
                 vertex < m_projected_vertices[mesh].size();
                 ++vertex) {
                if (std::abs(m_projected_depths[mesh][vertex].z) > 1.f) continue;
                const auto point = m_projected_vertices[mesh][vertex];
                const float dx = point.x - x;
                const float dy = point.y - y;
                const float distance = std::sqrt(dx * dx + dy * dy);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = {mesh, vertex};
                }
            }
        }
        if (!best)
            return false;
        set_active_mesh(best->first);
        if (!extend) m_selected_vertices.clear();
        if (!m_selected_vertices.erase(best->second)) m_selected_vertices.insert(best->second);
        set_status("Vértice " + std::to_string(best->second + 1) +
            " selecionado.");
        return true;
    }

    bool select_edge(const float x, const float y, bool extend) {
        float best_distance = 8.f;
        std::optional<std::pair<std::size_t, mesh_edge>> best;
        for (std::size_t mesh_index = 0;
             mesh_index < m_model.meshes.size();
             ++mesh_index) {
            std::set<mesh_edge> edges;
            for (const auto& face : m_model.meshes[mesh_index].faces)
                for (std::size_t corner = 0; corner < face.size(); ++corner)
                    edges.insert(ordered_mesh_edge(
                        face[corner], face[(corner + 1) % face.size()]
                    ));
            for (const auto& edge : edges) {
                if (std::abs(m_projected_depths[mesh_index][edge.first].z) > 1.f ||
                    std::abs(m_projected_depths[mesh_index][edge.second].z) > 1.f) continue;
                const auto first = m_projected_vertices[mesh_index][edge.first];
                const auto second = m_projected_vertices[mesh_index][edge.second];
                const float dx = second.x - first.x;
                const float dy = second.y - first.y;
                const float length_squared = dx * dx + dy * dy;
                if (length_squared < 1e-5f)
                    continue;
                const float t = std::clamp(
                    ((x - first.x) * dx + (y - first.y) * dy) /
                        length_squared,
                    0.f,
                    1.f
                );
                const float distance_x = x - (first.x + t * dx);
                const float distance_y = y - (first.y + t * dy);
                const float distance = std::sqrt(
                    distance_x * distance_x + distance_y * distance_y
                );
                if (distance < best_distance) {
                    best_distance = distance;
                    best = {mesh_index, edge};
                }
            }
        }
        if (!best)
            return false;
        set_active_mesh(best->first);
        if (!extend) m_selected_edges.clear();
        if (!m_selected_edges.erase(best->second)) m_selected_edges.insert(best->second);
        set_status("Aresta selecionada.");
        return true;
    }

    bool parse_extrude_distance(float& value) const {
        if (!m_extrude_distance)
            return false;
        const auto& text = m_extrude_distance->get_buffer();
        const auto parsed = std::from_chars(
            text.data(),
            text.data() + text.size(),
            value,
            std::chars_format::general
        );
        return parsed.ec == std::errc{} &&
            parsed.ptr == text.data() + text.size() &&
            std::isfinite(value) && std::abs(value) > 1e-6f;
    }

    void extrude() {
        if (m_gizmo.is_dragging() || m_extrusion_before || m_cut_before) return;
        if (!is_native_model(m_source_path)) {
            set_status("Importe o modelo como .bmodel para editar a geometria.");
            return;
        }
        if (m_mode != model_selection_mode::faces ||
            m_selected_faces.empty() || m_active_mesh >= m_model.meshes.size()) {
            set_status("Selecione uma face no modo Faces para extrudar.");
            return;
        }
        float distance = 0.f;
        if (!parse_extrude_distance(distance)) {
            set_status("Informe uma distância de extrusão válida.");
            return;
        }
        std::string error;
        auto before = capture_geometry();
        if (!extrude_faces(
                m_model.meshes[m_active_mesh],
                m_selected_faces,
                distance,
                error
            )) {
            set_status(error);
            return;
        }
        m_selected_faces.clear();
        record_geometry(std::move(before));
        m_geometry_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        update_mesh_label();
        set_status("Extrusão aplicada. Salve o arquivo .bmodel.");
        mark_preview_dirty();
    }

    void start_loop_cut(bool pick_edge = false) {
        if (m_gizmo.is_dragging() || m_extrusion_before || m_cut_before) return;
        if (!is_native_model(m_source_path)) {
            set_status("Importe o modelo como .bmodel para editar a geometria.");
            return;
        }
        if (!pick_edge && m_mode == model_selection_mode::edges && m_selected_edges.size() == 1) {
            loop_cut();
            return;
        }
        set_selection_mode(model_selection_mode::edges);
        m_picking_cut = true;
        set_status("Clique em uma aresta para cortar. Esc cancela.");
    }

    void loop_cut() {
        if (m_gizmo.is_dragging() || m_extrusion_before || m_cut_before) return;
        if (!is_native_model(m_source_path)) {
            set_status("Importe o modelo como .bmodel para editar a geometria.");
            return;
        }
        if (m_mode != model_selection_mode::edges || m_selected_edges.size() != 1 ||
            m_active_mesh >= m_model.meshes.size()) {
            set_status("Loop Cut requer uma aresta selecionada no modo Edges.");
            return;
        }
        if (m_projection_dirty) return;
        const auto& points = m_projected_vertices[m_active_mesh];
        const auto selected = *m_selected_edges.begin();
        const auto edge = points[selected.second] - points[selected.first];
        const auto size = m_image->get_texture().m_size;
        const auto rect = m_image->processed_rect();
        if (size.x <= 0 || size.y <= 0) return;
        m_cut_screen_edge = {edge.x * rect.z / size.x, edge.y * rect.w / size.y};
        if (std::hypot(m_cut_screen_edge.x, m_cut_screen_edge.y) < 1.f) m_cut_screen_edge = {100.f, 0.f};
        std::string error;
        auto before = capture_geometry();
        if (!editor::ui::loop_cut(
                m_model.meshes[m_active_mesh],
                selected,
                error,
                &m_cut_rails
            )) {
            set_status(error);
            return;
        }
        m_selected_edges.clear();
        m_cut_before = std::move(before);
        m_cut_start = bgui::get_mouse_position();
        m_dirty = true;
        m_geometry_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        update_mesh_label();
        set_status("Deslize com o mouse. Clique para confirmar, botão direito para centralizar ou Esc para cancelar.");
        mark_preview_dirty();
    }

    bool geometry_operation_active() const {
        return m_context->is_playing || m_gizmo.is_dragging() || m_extrusion_before ||
            m_cut_before || m_picking_cut || m_selection_start;
    }

    void show_extract() {
        if (geometry_operation_active() || viewport_input_blocked_by_modal()) return;
        sync_selected_entity();
        if (m_model.meshes.empty()) {
            set_status("Selecione um objeto com malhas para extrair.");
            return;
        }
        const auto renderer = m_source_renderer;
        const auto source = m_source_path;
        const auto entity = m_model_entity;
        const auto project = m_context->project_root;
        const auto original = source.empty()
            ? std::filesystem::path("Modelo-" + std::to_string(entity.value_or(0)))
            : source;
        const auto base = original.stem().string();
        auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
        modal.add<bgui::text>("Extract: assets/models/" + base + "_parts", .3f);
        std::vector<std::string> meshes;
        for (std::size_t i = 0; i < m_model.meshes.size(); ++i)
            meshes.push_back("Malha " + std::to_string(i + 1));
        auto& choice = modal.content().add<bgui::combo_box>(std::move(meshes), .28f);
        choice.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        choice.style.layout.require_height(bgui::mode::pixel, 30.f);
        choice.set_selected_index(m_active_mesh);
        auto& name = modal.content().add<bgui::inputbox>(base + "-malha-" +
            std::to_string(m_active_mesh + 1), "Nome do arquivo .bmodel", .28f);
        modal.set_confirmation_text("Extract");
        modal.set_on_confirm([this, &name, &choice, renderer, source, original, base, entity, project] {
            sync_selected_entity();
            const auto index = static_cast<std::size_t>(choice.get_selected_index());
            const auto live = renderer.lock();
            if (geometry_operation_active() || !live || live != m_source_renderer.lock() ||
                !live->m_modelo || source != m_source_path || entity != m_model_entity ||
                project != m_context->project_root || index >= m_model.meshes.size() ||
                index >= live->m_modelo->meshes.size()) {
                set_status("O modelo mudou. Abra Extract novamente.");
                return;
            }
            try {
                const auto output = extract(project, original, name.get_buffer(),
                    m_model.meshes[index], live->m_modelo->meshes[index].getMaterial());
                const auto message = "Malha extraída em assets/models/" + base + "_parts/" +
                    output.filename().string();
                set_status(message);
                if (m_on_extract) m_on_extract(message);
            } catch (const std::exception& error) {
                set_status(error.what());
            }
        });
        modal.add<bgui::button>("Cancelar", .28f, [surface = &modal] {
            bgui::add_function([surface] { bgui::get_layout().remove(surface); });
        });
    }

public:
    void save() {
        if (geometry_operation_active()) return;
        sync_selected_entity();
        if (m_model.meshes.empty() || !m_model_entity) {
            set_status("Selecione uma entidade com modelo antes de salvar.");
            return;
        }
        const auto& destination = m_source_path;
        if (!is_native_model(destination) || !std::filesystem::is_regular_file(destination)) {
            set_status("O arquivo .bmodel existente é necessário para salvar.");
            return;
        }
        std::string error;
        if (auto renderer=m_source_renderer.lock(); renderer && renderer->m_modelo && renderer->m_modelo->meshes.size()==m_model.meshes.size()) {
            for (std::size_t i=0;i<m_model.meshes.size();++i) {
                std::filesystem::path material_file;
                if (!commons::store_material_asset(destination.parent_path()/(destination.stem().string()+".materials"),
                    renderer->m_modelo->meshes[i].getMaterial(),material_file,error)) { set_status(error);return; }
                m_model.meshes[i].material_path=material_file.lexically_relative(destination.parent_path()).generic_string();
            }
        }
        if (!commons::save_editable_model(destination, m_model, error)) {
            set_status(error);
            return;
        }

        bool updated_entity = false;
        if (m_context && m_context->registry &&
            m_context->registry->is_alive(*m_model_entity)) {
            const auto renderer =
                m_context->registry->get<commons::renderer>(*m_model_entity);
            if (renderer && renderer == m_source_renderer.lock() && renderer->m_modelo &&
                renderer->m_modelo->get_source_path() == m_source_path.string()) {
                if (renderer->m_modelo->meshes.size() != m_model.meshes.size()) {
                    set_status("Modelo salvo; a geometria do objeto mudou durante a edição.");
                    return;
                }
                for (std::size_t i = 0; i < m_model.meshes.size(); ++i) {
                    auto& live = renderer->m_modelo->meshes[i];
                    const auto& vertices = m_model.meshes[i].vertices;
                    const auto indices = m_model.meshes[i].triangulated_indices();
                    if (live.indices() == indices &&
                        std::equal(vertices.begin(), vertices.end(), live.vertices().begin(), live.vertices().end(),
                            [](const auto& a, const auto& b) {
                                return a.position == b.position && a.normal == b.normal && a.uvcoords == b.uvcoords;
                            }))
                        continue;
                    if (live.isLoaded()) live.unload();
                    live.set_vertices(vertices);
                    live.set_indices(indices);
                }
                renderer->m_modelo->relocate_source_path(destination);
                updated_entity = true;
            }
        }
        m_saved_geometry_revision = m_geometry_revision;
        m_dirty = false;
        update_mesh_label();
        if (m_source_label) m_source_label->set_buffer(destination.filename().string());
        set_status(updated_entity
            ? "Modelo salvo: " + destination.filename().string()
            : "Modelo salvo, mas a entidade não está mais disponível na cena.");
    }
};

}
