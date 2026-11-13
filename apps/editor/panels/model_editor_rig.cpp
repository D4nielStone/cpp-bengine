#include "model_editor_view.hpp"
#include "model_rig_edit.hpp"
#include <random>
#include <tuple>

namespace editor::ui {
namespace {
commons::rig_joint* find_joint(commons::model_rig& rig, const std::string& id) {
    const auto found = std::find_if(rig.joints.begin(), rig.joints.end(), [&](const auto& joint) { return joint.id == id; });
    return found == rig.joints.end() ? nullptr : &*found;
}
}
bool model_editor_view::rig_editable() const {
    if (!m_context || m_context->is_playing || !m_model_entity ||
        m_context->selected_entity != m_model_entity || m_context->locked_entities.contains(*m_model_entity) ||
        !is_native_model(m_source_path) || geometry_operation_active()) return false;
    auto source = m_source_renderer.lock();
    return source && source == m_context->registry->get<commons::renderer>(*m_model_entity) &&
        source->m_modelo && source->m_modelo == m_source_model;
}
bool model_editor_view::refresh_authored_rig() {
    if (!is_native_model(m_source_path)) return true;
    auto source = m_source_renderer.lock();
    if (!source || !source->m_modelo || source->m_modelo != m_source_model || !m_model_entity ||
        m_context->registry->get<commons::renderer>(*m_model_entity) != source) return false;
    if (m_model.rig.joints.empty() && !source->m_modelo->has_skeleton()) return true;
    std::string error;
    if (!source->m_modelo->set_rig(m_model, error)) { set_status(error); return false; }
    m_source_pose_revision = source->m_modelo->pose_revision();
    m_rig_preview = source->m_modelo->clone();
    if (m_rig_edit_rest) m_rig_preview->reset_pose();
    m_rig_playing = false;
    sample_rig();
    return true;
}
bool model_editor_view::commit_rig_definition(commons::editable_model_data next) {
    if (!rig_editable()) { set_status(tr("Selecione um modelo .bmodel editável e pare a reprodução.")); return false; }
    std::string error;
    if (!next.validate(error)) { set_status(error); return false; }
    auto before = capture_geometry();
    m_model = std::move(next);
    if (!refresh_authored_rig()) { m_model = std::move(before.model); return false; }
    record_geometry(std::move(before));
    m_show_skeleton = true;
    m_geometry_dirty = m_projection_dirty = true;
    update_mesh_label();
    mark_preview_dirty();
    set_status(tr("Rig modificado. Salve o .bmodel antes de gravar animações da pose."));
    return true;
}
void model_editor_view::create_rig_joint(bool child) {
    if (!rig_editable()) return;
    auto next = m_model;
    std::random_device random;
    std::string id;
    do { id = "bone-" + std::to_string(random()) + "-" + std::to_string(random()); }
    while (find_joint(next.rig, id));
    commons::rig_joint joint{id, "Osso " + std::to_string(next.rig.joints.size() + 1), "", {}};
    if (child && find_joint(next.rig, m_active_joint)) {
        joint.parent = m_active_joint;
        joint.rest.position = {0,1,0};
    }
    next.rig.joints.push_back(std::move(joint));
    m_rig_edit_rest = true;
    if (commit_rig_definition(std::move(next))) m_active_joint = std::move(id);
}
void model_editor_view::build_rig_controls() {
    auto source = m_source_renderer.lock();
    if (!source || !source->m_modelo || m_model.meshes.empty() || !m_model_entity) return;
    auto& section = m_mesh_controls->add<bgui::tree>(tr("Rig e pose"), .26f, m_rig_section_expanded);
    section.add_class("model-rig-section");
    auto& content = section.children();
    content.style.layout.padding = {6,3,6,3};
    content.style.visual.visible = true;
    content.style.visual.background.normal = bgui::color{.065f,.065f,.065f,1.f};
    const bool native = is_native_model(m_source_path);
    const auto& joints = native ? m_model.rig.joints : source->m_modelo->rig_joints();
    if (native) {
        auto& add = content.add<bgui::button>(tr(joints.empty() ? "Criar rig" : "Adicionar osso raiz"), .25f,
            [this] { create_rig_joint(false); });
        add.set_tooltip(tr("Adicionar osso raiz"), tr("Cria um osso independente na origem do modelo. Ajuste o repouso e vincule os vértices antes de animar."));
        add.set_enable(!m_context->is_playing && !m_context->locked_entities.contains(*m_model_entity));
    }
    if (joints.empty()) {
        content.add<bgui::text>(tr(native ? "Crie os ossos, ajuste a hierarquia e vincule a malha ao rig." :
            "Este modelo não possui nós ou ossos editáveis. Importe uma cópia .bmodel para criar um rig."), .24f);
        return;
    }
    if (std::none_of(joints.begin(), joints.end(), [&](const auto& joint) { return joint.id == m_active_joint; }))
        m_active_joint = joints.front().id;
    const auto selected = std::find_if(joints.begin(), joints.end(), [&](const auto& joint) { return joint.id == m_active_joint; });
    std::vector<std::string> names, ids;
    std::size_t index = 0;
    for (const auto& joint : joints) {
        if (joint.id == m_active_joint) index = ids.size();
        names.push_back(joint.name); ids.push_back(joint.id);
    }
    auto& chooser = content.add<bgui::combo_box>(names, .25f, index);
    chooser.add_class("model-rig-joints");
    chooser.style.layout.require_height(bgui::mode::pixel, 24.f);
    chooser.set_on_change([this, ids](std::size_t selected, const std::string&) {
        if (selected >= ids.size()) return;
        m_active_joint = ids[selected];
        m_material_revision = std::numeric_limits<std::size_t>::max();
        mark_preview_dirty();
    });
    content.add<bgui::button>(tr("Mostrar / ocultar esqueleto"), .24f, [this] {
        m_show_skeleton = !m_show_skeleton; mark_preview_dirty();
    });
    if (native) content.add<bgui::button>(tr(m_rig_edit_rest ? "Editar pose" : "Editar repouso e pesos"), .25f, [this] {
        if (!m_rig_edit_rest || !m_dirty) {
            finish_weight_stroke(true);
            m_weight_paint = false;
            m_rig_edit_rest = !m_rig_edit_rest;
            if (!m_rig_edit_rest) set_selection_mode(model_selection_mode::object);
            auto source = m_source_renderer.lock();
            if (source && source->m_modelo) {
                m_rig_preview = source->m_modelo->clone();
                if (m_rig_edit_rest) m_rig_preview->reset_pose();
                sample_rig();
            }
            m_material_revision = std::numeric_limits<std::size_t>::max();
        } else set_status(tr("Salve o .bmodel antes de editar ou gravar a pose dos ossos."));
    });
    if (m_context->is_playing || m_context->locked_entities.contains(*m_model_entity)) {
        content.add<bgui::text>(tr("Pare a reprodução e desbloqueie a entidade para editar o rig."), .24f);
        return;
    }
    const auto id = m_active_joint;
    if (native && m_rig_edit_rest) {
        auto& name = content.add<bgui::field>(tr("Nome do osso"), selected->name, .25f,
            [this, id](const std::string& text) {
                auto next = m_model;
                if (auto* joint = find_joint(next.rig, id)) { joint->name = text; commit_rig_definition(std::move(next)); }
            });
        configure_property_field(name);
        std::vector<std::string> parents{tr("Sem pai")}, parent_ids{""};
        std::size_t parent_index = 0;
        for (const auto& joint : joints) if (joint.id != id) {
            if (joint.id == selected->parent) parent_index = parent_ids.size();
            parents.push_back(joint.name); parent_ids.push_back(joint.id);
        }
        auto& parent = content.add<bgui::combo_box>(parents, .25f, parent_index);
        parent.add_class("model-rig-parent");
        parent.set_tooltip(tr("Pai do osso"), tr("Define a hierarquia do rig. A transformação de repouso é relativa a este pai; ciclos são rejeitados."));
        parent.set_on_change([this, id, parent_ids](std::size_t index, const std::string&) {
            if (index >= parent_ids.size()) return;
            auto next = m_model;
            if (auto* joint = find_joint(next.rig, id)) {
                joint->parent = parent_ids[index];
                commit_rig_definition(std::move(next));
                m_material_revision = std::numeric_limits<std::size_t>::max();
            }
        });
        for (auto [label, member] : {std::pair{"Posição de repouso", &commons::rig_transform::position},
            {"Rotação de repouso", &commons::rig_transform::rotation}, {"Escala de repouso", &commons::rig_transform::scale}}) {
            content.add<bgui::text>(tr(label), .24f);
            auto& row = content.add<bgui::linear>(bgui::orientation::horizontal);
            configure_row(row); row.style.layout.padding = {0};
            for (int axis = 0; axis < 3; ++axis) {
                auto& field = row.add<bgui::field>(std::string(1, "XYZ"[axis]), format_value((selected->rest.*member)[axis]), .25f,
                    [this, id, member, axis](const std::string& text) {
                        float value{};
                        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
                            set_status(tr("Número inválido.")); return;
                        }
                        auto next = m_model;
                        if (auto* joint = find_joint(next.rig, id)) { (joint->rest.*member)[axis] = value; commit_rig_definition(std::move(next)); }
                    });
                configure_property_field(field);
                field.get_label().style.layout.require_width(bgui::mode::pixel, 12.f);
            }
        }
        content.add<bgui::button>(tr("Adicionar osso filho"), .25f, [this] { create_rig_joint(true); });
        content.add<bgui::button>(tr("Excluir osso"), .25f, [this, id] {
            auto next = m_model;
            if (std::any_of(next.rig.joints.begin(), next.rig.joints.end(), [&](const auto& joint) { return joint.parent == id; })) {
                set_status(tr("Reatribua ou remova os ossos filhos antes de excluir este osso.")); return;
            }
            std::erase_if(next.rig.joints, [&](const auto& joint) { return joint.id == id; });
            for (auto& mesh : next.meshes) for (auto& weights : mesh.skin) {
                std::erase_if(weights, [&](const auto& weight) { return weight.joint == id; });
                float total = 0; for (const auto& weight : weights) total += weight.weight;
                for (auto& weight : weights) weight.weight /= total;
            }
            commit_rig_definition(std::move(next));
        });
        auto& bind = content.add<bgui::button>(tr("Vincular automaticamente"), .25f, [this] {
            auto next = m_model; std::string error;
            if (bind_model_rig(next, error)) commit_rig_definition(std::move(next)); else set_status(error);
        });
        bind.set_tooltip(tr("Vincular malha ao rig"), tr("Distribui pesos normalizados entre até quatro ossos próximos de cada vértice. Depois ajuste a seleção manualmente."));
        auto& weight = content.add<bgui::field>(tr("Peso da seleção"), format_value(m_rig_weight), .25f,
            [this](const std::string& text) {
                float value{}; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value) && value >= 0 && value <= 1)
                    m_rig_weight = value;
                else set_status(tr("O peso deve estar entre zero e um."));
            });
        configure_property_field(weight);
        content.add<bgui::button>(tr("Aplicar peso aos vértices selecionados"), .25f, [this, id] {
            auto next = m_model; std::string error;
            if (set_rig_vertex_weight(next, m_active_mesh, m_selected_vertices, id, m_rig_weight, error))
                commit_rig_definition(std::move(next));
            else set_status(error);
        });
        auto& paint = content.add<bgui::button>(tr(m_weight_paint ? "Sair do Weight Paint" : "Weight Paint"), .25f, [this] {
            if (!rig_editable()) return;
            m_weight_paint = !m_weight_paint;
            m_selection_start.reset();
            m_picking_cut = false;
            m_material_revision = std::numeric_limits<std::size_t>::max();
            mark_preview_dirty();
        });
        paint.add_class("model-rig-weight-paint");
        for (auto [label, setting, maximum] : {
            std::tuple{"Raio do pincel (px)", &m_weight_radius, 500.f},
            std::tuple{"Força do pincel", &m_weight_strength, 1.f}}) {
            auto& field = content.add<bgui::field>(tr(label), format_value(*setting), .25f,
                [this, setting, maximum](const std::string& text) {
                    float value{}; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
                        std::isfinite(value) && value > 0 && value <= maximum) *setting = value;
                    else set_status(tr("Valor de pincel inválido."));
                });
            configure_property_field(field);
        }
        if (m_weight_paint) content.add<bgui::text>(tr("Arraste para pintar o peso da seleção. Azul = 0; vermelho = 1.\nA pintura atravessa a malha ativa. Esc cancela; Ctrl+Z desfaz a pincelada."), .24f);
        content.add<bgui::text>(tr("Repouso não deforma a malha.\nUse o modo Vértices para ajustar pesos.\nSalve o .bmodel antes de gravar a pose."), .24f);
    } else {
        if (native && m_dirty) { content.add<bgui::text>(tr("Salve o .bmodel antes de gravar a pose."), .24f); return; }
        const auto registry = m_context->registry;
        const auto entity = *m_model_entity;
        const auto owner = std::weak_ptr<commons::renderer>(source);
        const auto expected_model = source->m_modelo;
        const auto target = [registry, entity, owner, expected_model]() -> std::shared_ptr<commons::renderer> {
            auto value = registry->get<commons::renderer>(entity);
            return value && value == owner.lock() && value->m_modelo == expected_model ? value : nullptr;
        };
        for (auto [label, member] : {std::pair{"Posição", &commons::rig_transform::position},
            {"Rotação", &commons::rig_transform::rotation}, {"Escala", &commons::rig_transform::scale}}) {
            const auto read = [target, id, member] {
                if (auto value = target()) if (const auto pose = value->m_modelo->joint_pose(id)) return (*pose).*member;
                return bgui::vec3{};
            };
            add_vec_field(content, tr(label), read(), read, [target, id, member](std::size_t axis, float number) {
                if (auto value = target()) if (auto pose = value->m_modelo->joint_pose(id)) {
                    ((*pose).*member)[axis] = number;
                    value->m_modelo->set_joint_pose(id, *pose);
                    value->m_modelo->apply_pose();
                }
            });
        }
        content.add<bgui::text>(tr("Ative Gravar no painel Animação.\nAltere a pose deste osso para criar chaves.\nO clipe .banim move ossos sem mover a entidade."), .24f);
    }
}
void model_editor_view::finish_weight_stroke(bool cancel) {
    if (!m_weight_before) return;
    if (!cancel && !m_weight_influences.empty() && refresh_authored_rig()) {
        record_geometry(std::move(*m_weight_before));
        set_status(tr("Pesos pintados. Salve o .bmodel para manter as alterações."));
    } else {
        m_model = std::move(m_weight_before->model);
        m_dirty = m_geometry_revision != m_saved_geometry_revision;
    }
    m_weight_before.reset();
    m_weight_influences.clear();
    mark_preview_dirty();
}

void model_editor_view::draw_weight_paint(bgui::vec2i mouse, bgui::vec2i previous, bool over,
    bool pressed, bool down, bool cancel) {
    if (cancel) { finish_weight_stroke(true); return; }
    if (!down) finish_weight_stroke(false);
    if (m_projection_dirty || m_active_mesh >= m_projected_vertices.size()) return;
    const auto rect = m_image->processed_rect();
    const auto size = m_image->get_texture().m_size;
    if (rect.z <= 0 || rect.w <= 0 || size.x <= 0 || size.y <= 0) return;
    const auto screen = [&](std::size_t vertex) {
        const auto point = m_projected_vertices[m_active_mesh][vertex];
        return bgui::vec2{rect.x + point.x * rect.z / size.x, rect.y + point.y * rect.w / size.y};
    };
    if (over && pressed && rig_editable()) {
        m_weight_before = capture_geometry();
        m_weight_influences.clear();
        previous = mouse;
    }
    if (over && down && m_weight_before) {
        const auto& baseline = m_weight_before->model.meshes[m_active_mesh];
        std::map<std::size_t, float> values;
        for (std::size_t vertex = 0; vertex < baseline.vertices.size(); ++vertex) {
            if (std::abs(m_projected_depths[m_active_mesh][vertex].z) > 1.f) continue;
            const float influence = rig_brush_influence(screen(vertex), {float(previous.x), float(previous.y)},
                {float(mouse.x), float(mouse.y)}, m_weight_radius * bgui::get_global_scale(), m_weight_strength);
            const auto found = m_weight_influences.find(vertex);
            if (influence <= (found == m_weight_influences.end() ? 0.f : found->second)) continue;
            const float original = baseline.skin.empty() ? 0.f : rig_vertex_weight(baseline.skin[vertex], m_active_joint);
            if (original == m_rig_weight) continue;
            values.emplace(vertex, original + (m_rig_weight - original) * influence);
            m_weight_influences[vertex] = influence;
        }
        if (!values.empty()) {
            std::string error;
            if (!set_rig_vertex_weights(m_model, m_active_mesh, values, m_active_joint, error)) {
                finish_weight_stroke(true); set_status(error); return;
            }
            m_dirty = true;
        }
    }
    auto& draw = bgui::get_draw_list();
    const auto color = draw.get_color();
    const auto clip = draw.get_clip_rect(); const bool clipped = draw.has_clip_rect();
    draw.set_clip_rect(clipped ? bgui::intersect_rect(clip, rect) : rect);
    const auto& mesh = m_model.meshes[m_active_mesh];
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        if (std::abs(m_projected_depths[m_active_mesh][vertex].z) > 1.f) continue;
        const float weight = mesh.skin.empty() ? 0.f : rig_vertex_weight(mesh.skin[vertex], m_active_joint);
        draw.set_color({weight, 1 - std::abs(2 * weight - 1), 1 - weight, 1.f});
        const auto p = screen(vertex);
        draw.add_rect_filled(p - bgui::vec2{3,3}, p + bgui::vec2{3,3});
    }
    if (over) {
        draw.set_color({1,1,1,1});
        constexpr int segments = 48;
        const float radius = m_weight_radius * bgui::get_global_scale();
        for (int i = 0; i < segments; ++i) {
            const float a = float(i) * 6.2831853f / segments, b = float(i + 1) * 6.2831853f / segments;
            draw.add_line({mouse.x + std::cos(a) * radius, mouse.y + std::sin(a) * radius},
                {mouse.x + std::cos(b) * radius, mouse.y + std::sin(b) * radius}, 1.f);
        }
    }
    draw.set_color(color);
    if (clipped) draw.set_clip_rect(clip); else draw.clear_clip_rect();
}

}
