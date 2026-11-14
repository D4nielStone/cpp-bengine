#pragma once
#include "../core/localization.hpp"
#include "panel.hpp"
#include "animation_editor.hpp"
#include "rig_entity_edit.hpp"
#include <charconv>
#include <chrono>
#include <sstream>
#include <map>

namespace editor::ui {

class animation_view final : public panel {
public:
    using clock = std::chrono::steady_clock;
    explicit animation_view(std::shared_ptr<editor_context> context,
        std::function<clock::time_point()> now = [] { return clock::now(); })
        : m_context(std::move(context)), m_edits(m_context), m_now(std::move(now)) {}
    ~animation_view() override { if (attached()) m_dock->remove_window(m_window); }

    void build(bgui::dock& dock) override {
        m_dock = &dock;
        bgui::panel_facade facade(dock, tr("Animação"), bgui::dock_area::bottom);
        m_window = &facade.native();
        m_window->add_class("editor-panel");
        auto body = facade.content();
        body.native().set_scrollable(false);
        body.native().style.layout.padding = bgui::vec4i{8, 5, 8, 5};
        body.native().add_class("animation-panel");
        body.native().style.visual.visible = true;
        body.native().style.visual.background.normal = bgui::color{.065f,.065f,.065f,1.f};
        m_identity = &body.label(tr("Selecione uma entidade na Hierarquia"), .25f);
        m_controls = &body.add<bgui::linear>(bgui::orientation::vertical);
        m_controls->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        m_controls->set_scrollable(true);
        m_controls->style.layout.limit_min = bgui::vec2i{0,0};
        m_controls->style.visual.visible = true;
        m_controls->style.visual.background.normal = bgui::color{.065f,.065f,.065f,1.f};
        auto& driver = body.add<refresh_driver, bgui::layer::overlay>();
        driver.refresh = [this] { update(); };
        driver.set_flex(false);
        driver.receives_input(false);
        for (const auto& [layer, elements] : dock.get_elements())
            for (const auto& element : elements)
                if (auto* files = dynamic_cast<bgui::window*>(element.get()); files && files->get_title().get_buffer() == tr("Arquivos")) {
                    dock.merge_window_as_tab(*files, *m_window);
                    break;
                }
        m_dirty = true;
    }
    void show() {
        if (!attached()) build(*m_dock);
        m_window->set_enable(true);
        auto config = m_dock->get_configuration();
        config.bottom_ratio = std::max(config.bottom_ratio, .52f);
        m_dock->apply_configuration(config);
        m_dock->focus_window(m_window);
    }
    void update() override {
        if (!attached() || !m_window->is_enabled()) { pause_preview(); return; }
        if (!m_dock->is_enabled()) {
            pause_preview();
            if (m_selected && m_recording) m_edits.begin_recording(*m_selected);
            return;
        }
        commons::synchronize_rig_entities(*m_context->registry);
        m_context->synchronize_selection();
        auto selected = m_context->selected_entity;
        if (selected == m_context->editor_camera_entity) selected.reset();
        if (selected != m_selected) {
            if (m_selected && m_recording) m_edits.record_changes(*m_selected, m_time);
            remember_session();
            pause_preview();
            m_selected = selected;
            m_time = 0; m_recording = false;
            m_asset = m_selected ? m_edits.clip(*m_selected) : nullptr;
            if (m_selected) {
                const auto found = m_sessions.find(*m_selected);
                if (found != m_sessions.end() && found->second.owner.lock() ==
                    m_context->registry->get<commons::transform>(*m_selected) &&
                    found->second.asset.lock() == m_asset.lock()) {
                    m_time = found->second.time;
                    m_recording = found->second.recording;
                }
                m_edits.begin_recording(*m_selected);
            }
            m_dirty = true;
        }
        if (m_context->is_playing) { pause_preview(); m_recording=false; }
        if (m_selected) {
            auto asset = m_edits.clip(*m_selected);
            if (asset != m_asset.lock()) { pause_preview(); m_asset = asset; m_time = 0; m_recording = false; m_dirty = true; }
            if (m_recording && m_edits.record_changes(*m_selected, m_time)) m_dirty = true;
        }
        const auto revision = m_context->history_revision + m_context->events.revision(editor_event::scene);
        if (revision != m_revision || m_playing != m_context->is_playing) {
            m_revision = revision; m_playing = m_context->is_playing; m_dirty = true;
        }
        if (m_preview_running && (revision != m_preview_scene_revision ||
            m_context->undo_stack.size() != m_preview_history_size || !m_selected ||
            m_context->locked_entities.contains(*m_selected) || (m_time_input && m_time_input->is_focused()) || m_edits.player(*m_selected) != m_preview_player.lock())) pause_preview();
        if (m_preview_running) advance_preview();
        if (m_dirty) { m_dirty = false; rebuild(); }
    }
    double time() const { return m_time; }
    void set_import_browser(std::function<std::filesystem::path(std::string&)> browse) { m_browse_import = std::move(browse); }

private:
    struct session {
        std::weak_ptr<commons::transform> owner;
        std::weak_ptr<commons::animation_clip> asset;
        double time{};
        bool recording{};
    };
    std::map<uint32_t, session> m_sessions;
    void remember_session() {
        std::erase_if(m_sessions, [](const auto& entry) { return entry.second.owner.expired(); });
        if (m_selected) m_sessions[*m_selected] = {
            m_context->registry->get<commons::transform>(*m_selected), m_asset, m_time, m_recording};
    }
    class refresh_driver : public bgui::element {
    public:
        std::function<void()> refresh;
        void on_update() override { if (refresh) refresh(); }
        void get_requires(bgui::draw_data*) override {}
    };
    class timeline : public bgui::element {
    public:
        double duration{5}, cursor{};
        std::vector<commons::animation_key> keys;
        std::function<void(double)> seek;
        std::function<void()> begin_drag;
        std::function<void()> end_scrub;
        bool scrubbing{};
        std::function<void(std::size_t, double)> move_key;
        std::optional<std::size_t> dragging;
        double drag_time{}, drag_x{}, original_time{};
        double original_x{};
        timeline() { type = "animation-timeline"; style.layout.limit_min = bgui::vec2i{0, 0}; receives_input(true); style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel); style.layout.require_height(bgui::mode::pixel, 46); }
        void on_clicked() override {
            bgui::element::on_clicked();
            dragging.reset(); scrubbing = false;
            const auto mouse = bgui::get_mouse_position();
            const double x = mouse.x;
            if (move_key && processed_width() > 0 && std::abs(mouse.y - (processed_y() + processed_height() / 2)) <= 10 * bgui::get_global_scale()) {
                double nearest = 9 * bgui::get_global_scale();
                for (std::size_t i = 0; i < keys.size(); ++i) {
                    const auto distance = std::abs(x - processed_x() - processed_width() * keys[i].time / duration);
                    if (distance < nearest) { nearest = distance; dragging = i; }
                }
                if (dragging) {
                    drag_x = x; original_x = processed_x(); original_time = drag_time = keys[*dragging].time;
                    if (begin_drag) begin_drag();
                    return;
                }
            }
            scrubbing = true;
            if (begin_drag) begin_drag();
            if (seek && processed_width() > 0) seek(std::clamp((x - processed_x()) / processed_width(), 0., 1.) * duration);
        }
        void set_drag(const bgui::vec2i&) override {
            if (dragging && processed_width() > 0)
                drag_time = std::clamp(original_time + (bgui::get_mouse_position().x - drag_x - processed_x() + original_x) * duration / processed_width(), 0., duration);
            else if (scrubbing && seek && processed_width() > 0)
                seek(std::clamp(double(bgui::get_mouse_position().x - processed_x()) / processed_width(), 0., 1.) * duration);
        }
        void on_released() override {
            bgui::element::on_released();
            const auto key = dragging;
            dragging.reset();
            if (key && move_key) move_key(*key, drag_time);
            if (scrubbing && end_scrub) end_scrub();
            scrubbing = false;
        }
        bool supports_render_cache() const override { return false; }
        void get_requires(bgui::draw_data* data) override {
            bgui::element::get_requires(data);
            auto& draw = data->m_draw_list;
            const auto previous_color = draw.get_color();
            const float x = processed_x(), y = processed_y(), width = processed_width(), height = processed_height();
            draw.set_color({.065f, .065f, .065f, 1});
            draw.add_quad({x,y}, {x+width,y}, {x+width,y+height}, {x,y+height});
            draw.set_color({.23f,.23f,.23f,1});
            const double seconds_per_pixel = duration / std::max(1.f, width);
            const double raw_step = std::max(.00001, 60 * seconds_per_pixel * bgui::get_global_scale());
            const double decade = std::pow(10., std::floor(std::log10(raw_step)));
            const double fraction = raw_step / decade;
            const double step = decade * (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10);
            const float left = std::max(x, float(data->m_clip_rect.x));
            const float right = std::min(x + width, float(data->m_clip_rect.x + data->m_clip_rect.z));
            for (double tick = std::floor((left - x) * seconds_per_pixel / step) * step;
                tick <= std::min(duration, (right - x) * seconds_per_pixel) + step; tick += step) {
                const float px = x + float(tick / duration * width);
                draw.add_line({px,y+8}, {px,y+height-4});
            }
            draw.set_color({.65f,.65f,.65f,1});
            for (std::size_t i = 0; i < keys.size(); ++i) {
                const float px = x + width * float((dragging == i ? drag_time : keys[i].time) / duration);
                const float middle = y + height / 2;
                draw.add_quad({px,middle-6}, {px+5,middle}, {px,middle+6}, {px-5,middle});
            }
            const float px = x + width * float(cursor / duration);
            draw.set_color({.80f,.80f,.80f,1}); draw.add_line({px,y+2},{px,y+height-1},2);
            draw.set_color(previous_color);
        }
    };
    bool attached() const {
        if (!m_dock) return false;
        for (const auto& [layer, elements] : m_dock->get_elements())
            for (const auto& element : elements) if (element.get() == m_window) return true;
        return false;
    }
    static bool number(const std::string& text, double& value) {
        const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data()+text.size() && std::isfinite(value);
    }
    void status(const std::string& text) { if (m_status) m_status->set_buffer(text); }
    void commit(commons::animation_clip next) {
        if (m_selected && m_context->selected_entity == m_selected && m_edits.commit(*m_selected, std::move(next))) { m_edits.begin_recording(*m_selected); m_dirty = true; }
        else status(tr("Alteração inválida; verifique tempo, duração e valor."));
    }
    commons::animation_clip current() const {
        auto clip = m_selected ? m_edits.clip(*m_selected) : nullptr;
        return clip ? *clip : commons::animation_clip{};
    }
    void refresh_playback_status() {
        const auto asset = m_asset.lock();
        if (!asset) return;
        status(tr(m_preview_running ? "REPRODUZINDO PRÉVIA · sem gravar keyframes; Ctrl+Z desfaz." : m_recording ? "GRAVANDO · altere os valores no Inspector; as chaves serão criadas automaticamente." :
            (asset->source_path.empty() ? "Animação antiga: Save Project criará o arquivo de clipe." :
            (asset->dirty() ? "Clipe modificado · Salvar clipe ou Save Project para gravar o arquivo." :
             "Ative Gravar e altere propriedades no Inspector. Clique na régua para visualizar."))));
    }
    void sync_playback_controls() {
        if (m_plot) m_plot->cursor = m_time;
        for (auto* plot : m_track_plots) plot->cursor = m_time;
        if (m_time_input && !m_time_input->is_focused()) m_time_input->set_buffer(std::to_string(m_time));
        if (m_preview_button) m_preview_button->get_label().set_buffer(tr(m_preview_running ? "Pausar prévia" : "Reproduzir prévia"));
        if (m_record_button) {
            m_record_button->get_label().set_buffer(tr(m_recording ? "Parar gravação" : "● Gravar"));
            m_record_button->remove_class(m_recording ? "primary" : "danger");
            m_record_button->add_class(m_recording ? "danger" : "primary");
        }
    }
    void seek(double time, bool continuous = false) {
        if (m_preview_running || !continuous) pause_preview();
        const auto asset = m_asset.lock();
        m_time = std::clamp(time, 0., asset ? asset->duration : 5.);
        if (m_selected) {
            m_edits.preview(*m_selected, m_time, continuous);
            m_edits.begin_recording(*m_selected);
        }
        sync_playback_controls();
    }
    void pause_preview() {
        const bool was_running = m_preview_running;
        m_preview_running = false;
        m_edits.end_preview();
        sync_playback_controls();
        if (was_running) refresh_playback_status();
    }
    void start_preview() {
        if (!m_selected) return;
        auto asset = m_edits.clip(*m_selected);
        if (!asset || asset->tracks.empty()) { status("Nenhuma trilha para visualizar."); return; }
        m_recording = false;
        if (m_time >= asset->duration) m_time = 0;
        m_edits.end_preview();
        if (!m_edits.preview(*m_selected, m_time, true)) { status("Nenhuma trilha válida."); m_dirty = true; return; }
        m_preview_running = true;
        m_preview_player = m_edits.player(*m_selected);
        m_preview_scene_revision = m_context->history_revision + m_context->events.revision(editor_event::scene);
        m_preview_history_size = m_context->undo_stack.size();
        m_last_preview = m_now();
        sync_playback_controls();
        refresh_playback_status();
    }
    void advance_preview() {
        const auto now = m_now();
        const double dt = std::chrono::duration<double>(now - m_last_preview).count();
        m_last_preview = now;
        auto asset = m_asset.lock();
        auto player = m_preview_player.lock();
        if (!asset || !player || !std::isfinite(asset->duration) || asset->duration <= 0 ||
            !std::isfinite(player->speed) || player->speed < 0) { pause_preview(); return; }
        if (!std::isfinite(dt) || dt <= 0) return;
        const double next = m_time + dt * player->speed;
        if (!std::isfinite(next)) { pause_preview(); return; }
        m_time = asset->loop ? std::fmod(next, asset->duration) : std::min(next, asset->duration);
        if (!m_edits.preview(*m_selected, m_time, true)) { pause_preview(); return; }
        sync_playback_controls();
        if (!asset->loop && m_time >= asset->duration) pause_preview();
    }
    static bgui::panel_content control_row(bgui::panel_content body) {
        auto row = body.row();
        row.native().add_class("animation-control-row");
        row.native().style.layout.limit_min = bgui::vec2i{480, 0};
        row.native().style.layout.require_height(bgui::mode::pixel, 26.f);
        row.native().style.layout.padding = bgui::vec4i{0};
        row.native().style.layout.margin = bgui::vec4i{0, 1, 0, 2};
        row.native().style.layout.align = {bgui::alignment::start, bgui::alignment::center};
        return row;
    }
    void set_timeline_zoom(double zoom) {
        m_timeline_zoom = std::clamp(zoom, .25, 8.);
        if (!m_timeline_canvas) return;
        const auto asset = m_asset.lock();
        const double base = std::clamp((asset ? asset->duration : 5.) * 100., 760., 200000.);
        m_timeline_canvas->style.layout.require_width(bgui::mode::pixel, float(std::max(480., base * m_timeline_zoom)));
        m_timeline_canvas->mark_style_dirty();
    }
    void align_controls() {
        const auto visit = [&](const auto& self, bgui::layout& parent) -> void {
            for (auto& [layer, children] : parent.get_elements()) for (auto& child : children) {
                if (parent.has_class("animation-control-row")) {
                    if (auto* field = dynamic_cast<bgui::field*>(child.get())) {
                        field->style.layout.require_height(bgui::mode::pixel, 22.f);
                        field->style.layout.padding = bgui::vec4i{0};
                        field->style.layout.margin = bgui::vec4i{0,0,6,0};
                        field->get_label().style.layout.require_mode(bgui::mode::pixel, bgui::mode::match_parent);
                        field->get_label().style.layout.require_width(bgui::mode::pixel, 78.f);
                        field->get_inputbox().style.layout.require_height(bgui::mode::pixel, 22.f);
                    } else if (auto* button = dynamic_cast<bgui::button*>(child.get())) {
                        button->style.layout.require_height(bgui::mode::pixel, 22.f);
                        button->style.layout.padding = bgui::vec4i{8,0,8,0};
                        button->style.layout.margin = bgui::vec4i{0,0,6,0};
                        button->style.layout.align = {bgui::alignment::center,bgui::alignment::center};
                    }
                }
                if (auto* nested = child->as_layout()) self(self, *nested);
            }
        };
        visit(visit, *m_controls);
    }
    void rebuild() {
        m_status = nullptr;
        m_plot = nullptr;
        m_track_plots.clear();
        m_time_input = nullptr;
        m_preview_button = m_record_button = nullptr;
        m_timeline_canvas = nullptr;
        for (auto& [layer, elements] : m_controls->get_elements())
            while (!elements.empty()) m_controls->remove(elements.back().get());
        auto body = bgui::panel_content(*m_controls);
        if (!m_selected || m_context->is_playing || m_context->locked_entities.contains(*m_selected)) {
            m_identity->set_buffer(tr("Animação · selecione uma entidade editável"));
            body.label(tr("Use a Hierarquia para escolher a entidade. Pare o jogo para editar keyframes."), .25f);
            return;
        }
        const auto id = *m_selected;
        const auto name = m_context->entity_names.find(id);
        m_identity->set_buffer("ANIMAÇÃO  /  " + (name == m_context->entity_names.end() ? "Entidade " + std::to_string(id) : name->second));
        if (const auto renderer = m_context->registry->get<commons::renderer>(id);
            renderer && renderer->m_modelo && renderer->m_modelo->has_skeleton()) {
            body.action(tr("Expor ossos na Hierarquia"), [this, id] {
                expose_rig_entities(m_context, id); m_dirty = true;
            }, .24f);
            body.label(tr("Selecione um osso na Hierarquia e ative Gravar para animar sua transformação."), .24f);
        }
        auto file = control_row(body);
        auto player = m_edits.player(id);
        auto asset = m_edits.clip(id);
        m_asset = asset;
        auto& path = file.property(tr("Clipe"), player ? player->clip : "", {}, .24f);
        path.style.layout.require_width(bgui::mode::stretch);
        auto* input = &path.get_inputbox();
        auto file_actions = control_row(body);
        file_actions.action(tr("Atribuir .banim"), [this, id, input] {
            std::string error;
            if (m_edits.assign(id, input->get_buffer(), error)) { m_recording = false; m_dirty = true; }
            else status(error);
        }, .24f);
        file_actions.action(tr("Importar .banim"), [this, id, input] {
            std::string error;
            const auto source = m_browse_import ? m_browse_import(error) : std::filesystem::path(input->get_buffer());
            if (source.empty()) { if (!error.empty()) status(error); return; }
            if (m_edits.import_clip(id, source, error)) { m_recording = false; m_dirty = true; }
            else status(error);
        }, .24f);
        file_actions.action(tr("Salvar clipe"), [this, id] {
            std::string error;
            status(m_edits.save(id,error) ? "Clipe salvo. Save Project salva a referência na cena." : error);
        }, .24f);
        file_actions.action(tr("Recarregar"), [this,id] {
            std::string error;
            if(m_edits.reload(id,error)) {m_recording=false; m_dirty=true;}
            else status(error);
        },.24f);
        auto create = control_row(body);
        auto* clip_name_input = &create.add<bgui::inputbox>("Animation", tr("Nome do novo clipe"), .30f);
        clip_name_input->style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);
        create.action(asset ? tr("Duplicar clipe") : tr("Criar clipe"), [this, id, clip_name_input] {
            std::string error;
            if (m_edits.create(id, clip_name_input->get_buffer(), error)) { m_recording = false; m_dirty = true; }
            else status(error);
        }, .24f);
        if (!player) create.action(tr("Adicionar componente Animation"), [this, id] {
            if (m_edits.add_component(id)) m_dirty = true;
        }, .24f);
        m_status = &body.label("", .25f);
        m_status->add_class("animation-status");
        m_status->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        if (!asset) {
            body.label(player ? tr("Crie ou atribua um arquivo .banim ao componente Animation.") :
                "Adicione Animation à entidade e crie um clipe para começar.", .25f);
            if (!m_edits.error().empty()) status(m_edits.error());
            align_controls();
            return;
        }
        auto clip = *asset;
        m_time = std::clamp(m_time, 0., clip.duration);
        auto settings = control_row(body);
        settings.property(tr("Duração (s)"), std::to_string(clip.duration), [this](const std::string text) {
            double value; if (!number(text,value) || value <= 0 || value > 86400) { status("Duração inválida."); return; }
            auto next = current(); next.duration = value; commit(next);
        }, .24f).style.layout.require_width(bgui::mode::pixel, 200.f);
        settings.toggle("Loop", clip.loop, [this](bool loop) { auto next = current(); next.loop = loop; commit(next); }, .24f)
            .style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::match_parent);
        settings.toggle("Autoplay", player->autoplay, [this, id](bool autoplay) {
            auto p = m_edits.player(id); if (p && m_edits.settings(id,autoplay,p->speed)) m_dirty=true;
        }, .24f).style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::match_parent);
        settings.property(tr("Velocidade"), std::to_string(player->speed), [this, id](const std::string text) {
            double value; auto p=m_edits.player(id);
            if (!number(text,value) || !p || !m_edits.settings(id,p->autoplay,value)) status("Velocidade inválida (0 a 100).");
            else m_dirty=true;
        }, .24f).style.layout.require_width(bgui::mode::stretch);
        auto actions = control_row(body);
        auto& record = actions.action(m_recording ? tr("Parar gravação") : tr("● Gravar"), [this, id] {
            pause_preview();
            m_recording = !m_recording;
            m_edits.begin_recording(id);
            sync_playback_controls();
            refresh_playback_status();
        }, .25f);
        m_record_button = &record;
        record.add_class(m_recording ? "danger" : "primary");
        auto& time_field = actions.property(tr("Tempo (s)"), std::to_string(m_time), [this](const std::string text) {
            double time; if (number(text,time) && time >= 0 && time <= current().duration) seek(time);
            else status("Tempo fora da duração.");
        }, .24f);
        time_field.style.layout.require_width(bgui::mode::pixel, 180.f);
        m_time_input = &time_field.get_inputbox();
        actions.action(tr("Aplicar prévia"), [this, id] {
            pause_preview();
            status(m_edits.preview(id,m_time) ? "Prévia aplicada; Ctrl+Z desfaz." : "Nenhuma trilha válida.");
        }, .24f);
        auto playback = control_row(body);
        m_preview_button = &playback.action(m_preview_running ? tr("Pausar prévia") : tr("Reproduzir prévia"), [this] {
            if (m_preview_running) pause_preview(); else start_preview();
        }, .24f);
        playback.action(tr("Parar prévia"), [this] { seek(0); }, .24f);
        playback.label(tr("Prévia na cena · Ctrl+Z desfaz"), .25f);
        playback.action("−", [this] { set_timeline_zoom(m_timeline_zoom / 1.5); }, .24f)
            .set_tooltip(tr("Diminuir zoom"), tr("Mostra um intervalo maior da animação na timeline."));
        playback.action("+", [this] { set_timeline_zoom(m_timeline_zoom * 1.5); }, .24f)
            .set_tooltip(tr("Aumentar zoom"), tr("Amplia o espaço entre os tempos para editar as chaves com precisão."));
        auto& scroll = body.add<bgui::linear>(bgui::orientation::vertical);
        scroll.add_class("animation-timeline-scroll");
        scroll.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        scroll.style.layout.limit_min = bgui::vec2i{0,160};
        scroll.style.layout.padding = bgui::vec4i{0,0,10,10};
        scroll.style.visual.visible = true;
        scroll.style.visual.background.normal = bgui::color{.045f,.045f,.045f,1.f};
        scroll.set_scrollable(true);
        m_timeline_canvas = &scroll.add<bgui::linear>(bgui::orientation::vertical);
        m_timeline_canvas->add_class("animation-timeline-content");
        m_timeline_canvas->style.layout.require_mode(bgui::mode::pixel, bgui::mode::wrap_content);
        m_timeline_canvas->style.layout.padding = bgui::vec4i{0};
        set_timeline_zoom(m_timeline_zoom);
        auto timeline_body = bgui::panel_content(*m_timeline_canvas);
        auto& plot=timeline_body.add<timeline>();
        m_plot = &plot;
        plot.duration=clip.duration; plot.cursor=m_time;
        for (const auto& track : clip.tracks) plot.keys.insert(plot.keys.end(),track.keys.begin(),track.keys.end());
        plot.seek=[this](double time) { seek(time, true); };
        plot.begin_drag = [this] { pause_preview(); };
        plot.end_scrub = [this] { m_edits.end_preview(); };
        auto ruler=timeline_body.row(); ruler.native().style.layout.limit_min = bgui::vec2i{0, 0}; ruler.native().style.layout.require_height(bgui::mode::pixel,18.f);
        ruler.label("0 s",.25f).style.layout.require_width(bgui::mode::stretch);
        ruler.label(std::to_string(clip.duration)+" s",.25f);
        refresh_playback_status();
        if (clip.tracks.empty()) timeline_body.label(tr("Nenhuma alteração gravada neste clipe."), .25f);
        else timeline_body.label(tr("Arraste os losangos na régua de cada trilha para mover as chaves."), .25f);
        for (std::size_t index=0; index<clip.tracks.size(); ++index) {
            const auto& track=clip.tracks[index];
            const auto names=commons::component::mapa_nomes_componentes.find(track.target);
            auto heading=control_row(timeline_body); heading.native().style.layout.require_height(bgui::mode::pixel,28.f);
            heading.label((names==commons::component::mapa_nomes_componentes.end() ? std::to_string(unsigned(track.target)) : names->second) + " / " + track.property,.24f)
                .style.layout.require_width(bgui::mode::stretch);
            heading.action(track.stepped ? tr("Constante") : tr("Linear"), [this,index] {
                auto next=current(); if(index>=next.tracks.size()) return;
                auto& track=next.tracks[index];
                bool discrete=false;
                if (auto target=commons::animation_target(*m_context->registry,*m_selected,track.target))
                    for (const auto& property:commons::animation_properties(*target)) if(property.path==track.property) discrete=property.stepped;
                track.stepped=discrete || !track.stepped; commit(next);
            },.25f);
            heading.action(tr("Excluir trilha"),[this,index] {
                auto next=current(); if(index>=next.tracks.size()) return;
                next.tracks.erase(next.tracks.begin()+index); commit(next);
            },.25f);
            auto& track_plot = timeline_body.add<timeline>();
            m_track_plots.push_back(&track_plot);
            track_plot.style.layout.require_height(bgui::mode::pixel, 38);
            track_plot.duration = clip.duration; track_plot.cursor = m_time; track_plot.keys = track.keys;
            track_plot.begin_drag = [this] {
                const bool dirty = m_dirty;
                pause_preview(); m_recording = false; m_dirty = dirty;
                sync_playback_controls();
                refresh_playback_status();
            };
            const std::weak_ptr<commons::animation_clip> owner = asset;
            track_plot.move_key = [this, id, index, owner, player_owner = std::weak_ptr<commons::animation>(player), original = track](std::size_t key, double time) {
                auto asset = owner.lock();
                if (m_context->selected_entity != id || !asset || m_edits.player(id) != player_owner.lock() || m_edits.clip(id) != asset ||
                    index >= asset->tracks.size() || asset->tracks[index] != original) {
                    status(tr("A trilha mudou; tente mover a chave novamente.")); return;
                }
                if (m_edits.move_key(id, index, key, time)) m_dirty = true;
                else status(tr("Não foi possível mover: já existe uma chave neste tempo."));
            };
            track_plot.seek = [this](double time) { seek(time, true); };
            track_plot.end_scrub = [this] { m_edits.end_preview(); };
            for(std::size_t key=0; key<track.keys.size(); ++key) {
                const auto frame=track.keys[key];
                auto row=control_row(timeline_body);
                auto edit=[this,index,key](bool time,const std::string& text) {
                    auto next=current(); if(index>=next.tracks.size() || key>=next.tracks[index].keys.size()) return;
                    double value; if(!number(text,value)) { status("Número inválido."); return; }
                    auto& track=next.tracks[index]; auto frame=track.keys[key];
                    if(time) {
                        if(value<0 || value>next.duration) { status("Tempo fora da duração."); return; }
                        for(std::size_t i=0;i<track.keys.size();++i) if(i!=key && track.keys[i].time==value) {status("Já existe uma chave neste tempo."); return;}
                        frame.time=value;
                    } else {
                        if(auto target=commons::animation_target(*m_context->registry,*m_selected,track.target))
                            for(const auto& property:commons::animation_properties(*target)) if(property.path==track.property) {
                                if(value<property.minimum || value>property.maximum) {status("Valor fora dos limites."); return;}
                                if(property.stepped) value=std::round(value);
                            }
                        frame.value=value;
                    }
                    track.keys.erase(track.keys.begin()+key); track.insert(frame); commit(next);
                };
                row.property(tr("Tempo"),std::to_string(frame.time),[edit](const std::string text){edit(true,text);},.24f).style.layout.require_width(bgui::mode::stretch);
                row.property(tr("Valor"),std::to_string(frame.value),[edit](const std::string text){edit(false,text);},.24f).style.layout.require_width(bgui::mode::stretch);
                row.action(tr("Ir"),[this,frame]{seek(frame.time);},.24f);
                row.action(tr("Excluir"),[this,index,key]{
                    auto next=current(); if(index>=next.tracks.size() || key>=next.tracks[index].keys.size()) return;
                    auto& track=next.tracks[index]; track.keys.erase(track.keys.begin()+key);
                    if(track.keys.empty()) next.tracks.erase(next.tracks.begin()+index);
                    commit(next);
                },.24f);
            }
        }
        align_controls();
    }

    std::function<std::filesystem::path(std::string&)> m_browse_import;
    std::shared_ptr<editor_context> m_context;
    animation_editor m_edits;
    bgui::dock* m_dock{};
    bgui::window* m_window{};
    bgui::linear* m_controls{};
    bgui::text* m_identity{}, *m_status{};
    std::optional<uint32_t> m_selected;
    std::weak_ptr<commons::animation_clip> m_asset;
    std::function<clock::time_point()> m_now;
    clock::time_point m_last_preview{};
    bool m_preview_running{};
    std::weak_ptr<commons::animation> m_preview_player;
    uint64_t m_preview_scene_revision{};
    std::size_t m_preview_history_size{};
    timeline* m_plot{};
    std::vector<timeline*> m_track_plots;
    bgui::inputbox* m_time_input{};
    bgui::button* m_preview_button{}, *m_record_button{};
    bgui::linear* m_timeline_canvas{};
    double m_timeline_zoom{1.};
    bool m_recording{false};
    double m_time{};
    uint64_t m_revision{};
    bool m_dirty{true}, m_playing{};
};
}
