#pragma once

#include "editable_model_import.hpp"
#include "mesh_topology_edit.hpp"
#include "editor_context.hpp"
#include "panel.hpp"
#include "components/renderer.hpp"
#include "guizmo/transform_gizmo.hpp"
#include "viewport_input_state.hpp"
#include "../tools/camera_movement.hpp"
#include "mesh_material_editor.hpp"
#include "loaders/material_asset.hpp"
#include "systems/render_system.hpp"
#include <chrono>
#include <elem/tree.hpp>

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
        root.style.layout.padding = bgui::vec4i{10};

        m_selection_label = &root.add<bgui::text>("Nenhum objeto selecionado", .36f);
        m_selection_label->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_selection_label->receives_input(false);
        m_source_label = &root.add<bgui::text>("Selecione um objeto na cena ou na Hierarquia.", .25f);
        m_source_label->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_source_label->style.visual.text.normal = bgui::color{.6f, .6f, .6f, 1.f};
        m_source_label->receives_input(false);

        auto& modes = root.add<bgui::linear>(bgui::orientation::horizontal);
        modes.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        modes.style.layout.require_height(bgui::mode::pixel, 36.f);
        modes.style.layout.padding = bgui::vec4i{0};
        add_mode_button(modes, "Objeto", model_selection_mode::object);
        add_mode_button(modes, "Faces", model_selection_mode::faces);
        add_mode_button(modes, "Vértices", model_selection_mode::vertices);
        add_mode_button(modes, "Arestas", model_selection_mode::edges);

        auto& views = root.add<bgui::linear>(bgui::orientation::horizontal);
        views.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::pixel);
        views.style.layout.require_height(bgui::mode::pixel,32.f);
        for (int mode=0;mode<3;++mode) {
            const char* names[]{"Basic","Material","Render"};
            auto& button=views.add<bgui::button>(names[mode],.28f,[this,mode] { m_context->model_view_mode=mode; });
            button.style.layout.require_mode(bgui::mode::pixel,bgui::mode::pixel);
            button.style.layout.require_size(96.f,28.f);
            m_view_buttons.push_back(&button);
        }
        auto& body = root.add<bgui::linear>(bgui::orientation::horizontal);
        body.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        body.style.layout.padding = bgui::vec4i{0};
        m_image = &body.add<bgui::image>();
        m_image->add_class("model-editor-preview");
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
        tools.add<bgui::text>("Ferramentas da malha", .3f).receives_input(false);
        m_mesh_label = &tools.add<bgui::text>("Nenhum modelo", .26f);
        m_mesh_label->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        m_mesh_label->receives_input(false);
        tools.add<bgui::text>("Distância de extrusão", .26f).receives_input(false);
        m_extrude_distance = &tools.add<bgui::inputbox>("0.25", "Distância", .28f);
        m_extrude_distance->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
        m_extrude_distance->style.layout.require_height(bgui::mode::pixel, 28.f);
        m_extrude_distance->style.layout.limit_min = {0, 28};
        m_extrude_distance->style.layout.limit_max = {10000, 28};
        m_extrude_button = &tools.add<bgui::button>("Extrudar face", .28f, [this] { extrude(); });
        m_loop_button = &tools.add<bgui::button>("Loop Cut", .28f, [this] { loop_cut(); });
        m_save_button = &tools.add<bgui::button>("Salvar .bmodel", .28f, [this] { save(); });
        for (auto* button : {m_extrude_button, m_loop_button, m_save_button}) {
            button->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
            button->style.layout.require_height(bgui::mode::pixel, 30.f);
            button->style.layout.margin = bgui::vec4i{0, 4, 0, 0};
        }
        m_navigation_button = &tools.add<bgui::button>("Visão: Normal (O)", .28f, [this] {
            m_context->model_orbital_view = !m_context->model_orbital_view;
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
            "O: visão normal/orbital.\nBotão direito: olhar/orbitar.\nCtrl + Shift + arraste: pan.\nScroll: aproximar/afastar.\nG/R/S: transformar a seleção.\n\nFaces: selecione para extrudar.\nArestas: selecione para Loop Cut.\n\nAs alterações ficam pendentes até salvar.", .25f);
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
        refresh_mode_buttons();
        refresh_preview();
    }

    const commons::camera& preview_camera() const { return *m_preview_camera; }
    double last_preview_time_ms() const noexcept { return m_last_preview_ms; }
    std::size_t geometry_upload_count() const noexcept { return m_geometry_uploads; }
    std::size_t projection_update_count() const noexcept { return m_projection_updates; }

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
        if (m_context && static_cast<int>(m_mode) != m_context->model_selection) {
            m_gizmo_selection_dirty=true;
            m_mode = static_cast<model_selection_mode>(m_context->model_selection);
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
        if (m_view_mode!=m_context->model_view_mode) {
            m_view_mode=std::clamp(m_context->model_view_mode,0,2);
            m_material_dirty=true;mark_preview_dirty();
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
            const bool over = mouse_over_viewport(m_image) && !viewport_input_blocked_by_modal() &&
                !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus());
            const bool key = bgui::get_pressed(bgui::input_key::o);
            if (key && !m_orbit_key_down && over) m_context->model_orbital_view = !m_context->model_orbital_view;
            m_orbit_key_down = key;
            if (m_navigation->is_orbital() != m_context->model_orbital_view) {
                if (m_context->model_orbital_view) {
                    auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
                    m_navigation->focus_orbit_target(m_orbit_pivot, (eye->get_position()-m_orbit_pivot->get_position()).length());
                } else m_navigation->set_orbital(false);
            }
            m_navigation_button->get_label().set_buffer(m_context->model_orbital_view ? "Visão: Orbital (O)" : "Visão: Normal (O)");
            auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
            const auto before = eye->get_model_matrix();
            m_navigation_input->gizmo_active = m_gizmo.is_dragging();
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
    bgui::button* m_save_button{nullptr};
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
    std::optional<mesh_edge> m_selected_edge;
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
            for(const auto& mesh:m_model.meshes) meshes.emplace_back(mesh.vertices,mesh.triangulated_indices(),commons::material{});
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
            auto transform=m_context->registry->get<commons::transform>(*m_model_entity);
            if (transform) {
                const auto world=transform->get_world_model_matrix();
                if (std::abs(glm::determinant(world))<1e-8f) { set_status("Render requer uma transformação invertível.");return; }
                m_scene_preview_camera.config=m_preview_camera->config;
                if (m_context->editor_camera_entity) if(auto editor_camera=m_context->registry->get<commons::camera>(*m_context->editor_camera_entity)) {
                    const auto fov=m_scene_preview_camera.config.fov, near=m_scene_preview_camera.config.corte_curto, far=m_scene_preview_camera.config.corte_longo;
                    m_scene_preview_camera.config=editor_camera->config;
                    m_scene_preview_camera.config.fov=fov;m_scene_preview_camera.config.corte_curto=near;m_scene_preview_camera.config.corte_longo=far;
                    m_scene_preview_camera.config.flag_orth=false;
                    m_scene_preview_camera.skybox_asset_root=editor_camera->skybox_asset_root;
                    m_scene_preview_camera.skybox_faces=editor_camera->skybox_faces;
                }
                m_scene_preview_camera.viewMatrix=m_preview_camera->viewMatrix*glm::inverse(world);
                const auto eye=world*glm::vec4(m_preview_camera->position.x,m_preview_camera->position.y,m_preview_camera->position.z,1.f);
                m_scene_preview_camera.position={eye.x,eye.y,eye.z};
                camera=&m_scene_preview_camera;
            }
        }
        camera->request_framebuffer_size({width,height});
        if(m_view_mode==2 && m_model_entity)
            m_gpu_render_system.render_view(m_context->registry,*camera,*m_model_entity,m_gpu_renderer);
        else m_gpu_render_system.render_view(m_preview_registry,*camera);
        const auto texture=camera->framebuffer_texture();
        if(texture && (m_image->get_texture().m_id!=texture || m_image->get_texture().m_size!=bgui::vec2{float(width),float(height)}))
            m_image->set_external_texture(texture,{float(width),float(height)});
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
                group.children().style.layout.padding = bgui::vec4i{6,4,6,4};
                build_mesh_material_editor(*this, group.children(), m_context, renderer, i,
                    [this](auto* element, auto assign) { m_material_targets.emplace_back(element,std::move(assign)); },
                    [this](std::string message) { set_status(std::move(message)); mark_preview_dirty(); });
            }
        }
    }

    commons::transform m_gizmo_transform;
    std::vector<std::pair<std::size_t, std::size_t>> m_gizmo_vertices;
    bgui::vec2i m_gizmo_last_mouse{};
    bool m_gizmo_left_down{false};
    bool m_gizmo_selection_dirty{true};
    bool m_frame_preview{true};

    void draw_gizmo() {
        const auto mouse = bgui::get_mouse_position();
        const bool blocked = !m_context || m_context->is_playing || viewport_input_blocked_by_modal();
        const bool down = !blocked && bgui::get_pressed(bgui::input_key::mouse_left);
        const bool just_pressed = down && !m_gizmo_left_down;
        const auto delta = mouse - m_gizmo_last_mouse;
        m_gizmo_last_mouse = mouse;
        m_gizmo_left_down = down;
        const auto rect = m_image->processed_rect();
        const bool over = !blocked && !bgui::get_pressed(bgui::input_key::mouse_right) &&
            bgui::get_mouse_target() == m_pick_overlay &&
            !dynamic_cast<bgui::inputbox*>(bgui::get_keyboard_focus());
        if (m_gizmo_selection_dirty && !m_gizmo.is_dragging()) {
            m_gizmo_vertices.clear();
            bgui::vec3 pivot{};
            for (std::size_t mesh = 0; mesh < m_model.meshes.size(); ++mesh) {
                std::set<std::size_t> selected;
                if (m_mode == model_selection_mode::object && m_object_selected) {
                    for (std::size_t v = 0; v < m_model.meshes[mesh].vertices.size(); ++v) {
                        m_gizmo_vertices.emplace_back(mesh,v);pivot+=m_model.meshes[mesh].vertices[v].position;
                    }
                    continue;
                } else if (mesh == m_active_mesh) {
                    if (m_mode == model_selection_mode::faces)
                        for (auto face : m_selected_faces)
                            if (face < m_model.meshes[mesh].faces.size())
                                selected.insert(m_model.meshes[mesh].faces[face].begin(), m_model.meshes[mesh].faces[face].end());
                    if (m_mode == model_selection_mode::vertices) selected = m_selected_vertices;
                    if (m_mode == model_selection_mode::edges && m_selected_edge)
                        selected = {m_selected_edge->first, m_selected_edge->second};
                }
                for (auto vertex : selected) if (vertex < m_model.meshes[mesh].vertices.size()) {
                    m_gizmo_vertices.emplace_back(mesh, vertex);
                    pivot += m_model.meshes[mesh].vertices[vertex].position;
                }
            }
            if (!m_gizmo_vertices.empty()) pivot = pivot / static_cast<float>(m_gizmo_vertices.size());
            m_gizmo_transform.set_position(pivot);
            m_gizmo_transform.set_rotation({});
            m_gizmo_transform.set_scale({1.f, 1.f, 1.f});
            m_gizmo_selection_dirty = false;
        }
        const auto before = m_gizmo_transform.get_model_matrix();
        auto* target = m_gizmo_vertices.empty() ? nullptr : &m_gizmo_transform;
        const bool was_dragging = m_gizmo.is_dragging();
        m_gizmo.update(*m_preview_camera, target, rect, {rect}, mouse, delta,
            over, just_pressed && over, down && (over || m_gizmo.is_dragging()));
        const auto after = m_gizmo_transform.get_model_matrix();
        if (target && before != after) {
            const auto change = after * glm::inverse(before);
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
        if (!m_gizmo.is_dragging() && just_pressed && over) select_at(mouse);
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
            if(m_mode==model_selection_mode::edges && m_selected_edge) draw.add_line(screen(m_selected_edge->first),screen(m_selected_edge->second),3.f);
            if(m_mode==model_selection_mode::vertices) for(auto vertex:m_selected_vertices) { const auto p=screen(vertex);draw.add_rect_filled(p-bgui::vec2{3.f,3.f},p+bgui::vec2{3.f,3.f}); }
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

        m_gizmo = editor::transform_gizmo{};
        m_geometry_dirty=true;m_material_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        m_frame_preview = true;
        m_source_renderer = renderer;
        m_model_entity.reset();
        m_source_path.clear();
        m_model.meshes.clear();
        rebuild_mesh_controls();
        m_active_mesh = 0;
        m_selected_faces.clear();
        m_selected_vertices.clear();
        m_selected_edge.reset();
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
        // Preserve polygon topology when the asset matches the live geometry.
        bool loaded = false;
        auto extension = selected_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!selected_path.empty()) {
            loaded = extension == ".bmodel"
                ? commons::load_editable_model(selected_path, imported, error)
                : import_editable_model(selected_path, imported, error);
            if (loaded && imported.meshes.size() == renderer->m_modelo->meshes.size()) {
                for (std::size_t i = 0; i < imported.meshes.size() && loaded; ++i) {
                    const auto vertices = renderer->m_modelo->meshes[i].getVertices();
                    loaded = vertices.size() == imported.meshes[i].vertices.size() &&
                        renderer->m_modelo->meshes[i].getIndices() == imported.meshes[i].triangulated_indices();
                    for (std::size_t v = 0; v < vertices.size() && loaded; ++v)
                        loaded = vertices[v].position == imported.meshes[i].vertices[v].position;
                }
            } else loaded = false;
        }
        if (!loaded) {
            imported.meshes.clear();
            error.clear();
            for (const auto& source : renderer->m_modelo->meshes) {
                commons::editable_mesh_data mesh;
                mesh.vertices = source.getVertices();
                const auto indices = source.getIndices();
                if (indices.size() % 3 != 0) { error = "A malha contém índices incompletos."; break; }
                for (std::size_t i = 0; i < indices.size(); i += 3)
                    mesh.faces.push_back({indices[i], indices[i + 1], indices[i + 2]});
                imported.meshes.push_back(std::move(mesh));
            }
            loaded = error.empty() && imported.validate(error);
        }
        if (!loaded) {
            set_status(error);
            return;
        }
        m_model = std::move(imported);
        m_geometry_dirty=true;m_material_dirty=true;m_projection_dirty=true;
        rebuild_mesh_controls();
        m_active_mesh = 0;
        m_selected_faces.clear();
        m_selected_vertices.clear();
        m_selected_edge.reset();
        m_object_selected = true;
        update_mesh_label();
        set_status("Modelo do objeto selecionado. Escolha um modo de edição.");
        mark_preview_dirty();
    }

    void add_mode_button(
        bgui::linear& parent,
        const char* label,
        const model_selection_mode mode
    ) {
        auto& button = parent.add<bgui::button>(label, 0.27f, [this, mode]() {
            m_gizmo_selection_dirty=true;
            m_mode = mode;
            if (m_context) m_context->model_selection = static_cast<int>(mode);
            m_selected_faces.clear();
            m_selected_vertices.clear();
            m_selected_edge.reset();
            refresh_mode_buttons();
            mark_preview_dirty();
        });
        button.style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
        button.style.layout.require_size(96.f, 26.f);
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
            bgui::vec3 low{std::numeric_limits<float>::max()}, high{std::numeric_limits<float>::lowest()};
            for (const auto& mesh : m_model.meshes) for (const auto& v : mesh.vertices)
                for (int axis=0; axis<3; ++axis) { low[axis]=std::min(low[axis],v.position[axis]); high[axis]=std::max(high[axis],v.position[axis]); }
            const auto center = (low+high)*.5f;
            const float radius = std::max(.01f,(high-low).length()*.5f);
            const float aspect = static_cast<float>(width)/height;
            const float distance = radius / (std::sin(glm::radians(m_preview_camera->config.fov)*.5f)*std::min(1.f,aspect)) * 1.2f;
            m_preview_camera->config.corte_longo = std::max(300.f,distance+radius*4.f);
            m_preview_camera->config.corte_curto = std::max(1e-6f,distance*1e-4f);
            m_navigation->set_orbit_distance_limits(.01f,std::max(1000.f,distance*100.f));
            m_navigation->set_movement_speed(radius*2.f);
            m_navigation->set_zoom_sensitivity(radius*.1f);
            const float yaw = .65f - 1.57079632679f, pitch = -.4f;
            const bgui::vec3 forward{std::cos(pitch)*std::cos(yaw),std::sin(pitch),std::cos(pitch)*std::sin(yaw)};
            auto eye = m_preview_registry->get<commons::transform>(m_preview_camera->my_object);
            eye->set_position(center-forward*distance);
            eye->set_rotation({glm::degrees(pitch),glm::degrees(yaw),0.f});
            m_orbit_pivot->set_position(center);
            m_navigation->set_orbital(false);
            if (m_context->model_orbital_view) m_navigation->focus_orbit_target(m_orbit_pivot,distance);
            m_frame_preview = false;
        }
        m_preview_camera->viewportFBO = {width,height};
        m_preview_camera->getViewMatrix();
        m_preview_camera->obtProjectionMatrix();
        const auto matrix = m_preview_camera->projMatriz*m_preview_camera->viewMatrix;

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

    void select_at(const bgui::vec2i& position) {
        if (m_model.meshes.empty() || !m_image)
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
            m_active_mesh = selected.first;
            m_selected_faces = {selected.second};
            set_status("Face " + std::to_string(selected.second + 1) +
                " selecionada.");
        } else if (m_mode == model_selection_mode::vertices) {
            if (!select_vertex(x * width / std::max(1, image_rect.z),
                    y * height / std::max(1, image_rect.w)))
                return;
        } else {
            if (!select_edge(x * width / std::max(1, image_rect.z),
                    y * height / std::max(1, image_rect.w)))
                return;
        }
        m_gizmo_selection_dirty=true;
        update_mesh_label();
        mark_preview_dirty();
    }

    bool select_vertex(const float x, const float y) {
        float best_distance = 8.f;
        std::optional<std::pair<std::size_t, std::size_t>> best;
        for (std::size_t mesh = 0; mesh < m_projected_vertices.size(); ++mesh) {
            for (std::size_t vertex = 0;
                 vertex < m_projected_vertices[mesh].size();
                 ++vertex) {
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
        m_active_mesh = best->first;
        m_selected_vertices = {best->second};
        set_status("Vértice " + std::to_string(best->second + 1) +
            " selecionado.");
        return true;
    }

    bool select_edge(const float x, const float y) {
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
        m_active_mesh = best->first;
        m_selected_edge = best->second;
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
        m_dirty = true;m_geometry_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        update_mesh_label();
        set_status("Extrusão aplicada. Salve o modelo como .bmodel.");
        mark_preview_dirty();
    }

    void loop_cut() {
        if (m_mode != model_selection_mode::edges || !m_selected_edge ||
            m_active_mesh >= m_model.meshes.size()) {
            set_status("Loop Cut requer uma aresta selecionada no modo Edges.");
            return;
        }
        std::string error;
        if (!editor::ui::loop_cut(
                m_model.meshes[m_active_mesh],
                *m_selected_edge,
                error
            )) {
            set_status(error);
            return;
        }
        m_selected_edge.reset();
        m_dirty = true;m_geometry_dirty=true;m_projection_dirty=true;m_gizmo_selection_dirty=true;
        update_mesh_label();
        set_status("Loop Cut aplicado. Salve o modelo como .bmodel.");
        mark_preview_dirty();
    }

    void save() {
        if (m_model.meshes.empty() || !m_model_entity) {
            set_status("Selecione uma entidade com modelo antes de salvar.");
            return;
        }
        auto destination = m_source_path;
        std::error_code filesystem_error;
        if (destination.empty()) {
            if (!m_context || m_context->project_root.empty()) {
                set_status("Abra um projeto para salvar a geometria deste objeto.");
                return;
            }
            const auto directory = m_context->project_root / "models";
            std::filesystem::create_directories(directory, filesystem_error);
            if (filesystem_error) { set_status(filesystem_error.message()); return; }
            destination = directory / ("object-" + std::to_string(*m_model_entity) + ".bmodel");
        } else if (destination.extension() != ".bmodel") {
            destination.replace_extension(".bmodel");
        }
        if (destination != m_source_path) {
            const auto base = destination;
            unsigned int suffix = 2;
            while (std::filesystem::exists(destination, filesystem_error) && !filesystem_error)
                destination = base.parent_path() / (base.stem().string() + "-" +
                    std::to_string(suffix++) + ".bmodel");
            if (filesystem_error) { set_status(filesystem_error.message()); return; }
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
                    if (live.isLoaded()) live.unload();
                    live.set_vertices(m_model.meshes[i].vertices);
                    live.set_indices(m_model.meshes[i].triangulated_indices());
                }
                renderer->m_modelo->relocate_source_path(destination);
                updated_entity = true;
            }
        }
        m_source_path = destination;
        m_dirty = false;
        update_mesh_label();
        if (m_source_label) m_source_label->set_buffer(destination.filename().string());
        set_status(updated_entity
            ? "Modelo salvo: " + destination.filename().string()
            : "Modelo salvo, mas a entidade não está mais disponível na cena.");
    }
};

}
