#pragma once
#include "../core/localization.hpp"

#include "panel.hpp"
#include "scene_files.hpp"

#include <elem/modal.hpp>

namespace editor::ui {

class scenes_view final : public panel {
public:
    using open_action = std::function<void(const std::filesystem::path&)>;
    using file_action = std::function<void(const std::filesystem::path&, const std::string&)>;

    scenes_view(open_action open, file_action create, open_action erase,
        std::function<void(scenes_view&)> sync)
        : m_open(std::move(open)), m_create(std::move(create)), m_erase(std::move(erase)),
          m_sync(std::move(sync)) {}

    ~scenes_view() override {
        if (m_delete_modal) bgui::get_layout().remove(m_delete_modal);
        if (attached()) m_dock->remove_window(m_window);
    }

    void build(bgui::dock& parent) override {
        m_dock = &parent;
        auto& window = parent.add_window(tr("Cenas"), bgui::dock_area::left, .32f);
        m_window = &window;
        m_project.clear();
        window.add_class("editor-panel");
        auto& body = bgui::panel_content(window).add<surface>();
        body.before_update = [this] { update(); };
        body.add_class("editor-surface");
        body.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
        body.style.layout.padding = bgui::vec4i{12, 8, 12, 8};
        m_active_label = &body.add<bgui::text>(tr("Cena ativa"), .38f);
        m_name = &add_input(body, "", "Nome da cena nova ou duplicada", .33f);
        m_name->add_class("scene-file-name");
        auto& actions = body.add<bgui::linear>(bgui::orientation::horizontal);
        configure_row(actions);
        actions.add<bgui::button>(tr("Criar cena"), .33f, [this] { m_create({}, m_name->get_buffer()); }).add_class("primary");
        actions.add<bgui::button>(tr("Atualizar lista"), .33f, [this] { m_refresh = true; });
        actions.add<bgui::button>("Mostrar conexões", .32f, [this] { if(m_visual) m_visual(); });
        m_status = &body.add<bgui::text>("", .33f);
        m_list = &body.add<bgui::linear>(bgui::orientation::vertical);
        m_list->style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
        m_list->set_scrollable(true);
    }

    void set_visual_action(std::function<void()> action) { m_visual=std::move(action); }

    void show() {
        if (!attached()) build(*m_dock);
    }

    void set_project(const std::string& project, const std::filesystem::path& active, bool manifest) {
        if (!attached() || (project == m_project && active == m_active && manifest == m_manifest))
            return;
        if (m_project != project) {
            m_project = project;
            m_root = std::filesystem::absolute(project).lexically_normal().parent_path();
            m_name->set_buffer("");
            status("");
        }
        m_active = active;
        m_manifest = manifest;
        m_active_label->set_buffer("Cena ativa: " + display_name(active));
        m_refresh = true;
    }

    void status(const std::string& message) { if (attached()) m_status->set_buffer(message); }
    void refresh() { m_refresh = true; }

    void update() override {
        if (!attached()) return;
        m_sync(*this);
        if (!m_refresh || m_root.empty())
            return;
        m_refresh = false;
        for (auto& [layer, elements] : m_list->get_elements())
            while (!elements.empty())
                m_list->remove(elements.back().get());

        try {
            const auto scenes = scene_files::list(m_root, m_manifest);
            if (scenes.empty())
                m_list->add<bgui::text>(tr("Nenhuma cena encontrada na pasta do projeto."), .33f);
            for (const auto& path : scenes) {
                auto& row = m_list->add<bgui::linear>(bgui::orientation::horizontal);
                configure_row(row);
                row.add_class("scene-file-row");
                row.style.layout.padding = bgui::vec4i{10, 8, 10, 8};
                row.style.layout.margin = bgui::vec4i{0, 3, 0, 3};
                auto& name = row.add<bgui::text>(display_name(path), .33f);
                name.style.layout.align = bgui::vec<2, bgui::alignment>{bgui::alignment::start, bgui::alignment::center};
                name.style.layout.require_mode(
                    bgui::mode::stretch, bgui::mode::wrap_content);
                if (path == m_active) {
                    row.add<bgui::text>(tr("Cena ativa"), .30f).add_class("scene-active-badge");
                    row.style.visual.background.normal = bgui::color{.18f, .18f, .18f, 1.f};
                }
                if (path != m_active)
                    row.add<bgui::button>(m_manifest ? "Abrir e tornar inicial" : "Abrir", .32f,
                        [this, path] { m_open(path); });
                row.add<bgui::button>(tr("Duplicar"), .32f,
                    [this, path] { m_create(path, m_name->get_buffer()); });
                if (path != m_active)
                    row.add<bgui::button>(tr("Excluir..."), .32f, [this, path] { confirm_delete(path); });
            }
        } catch (const std::filesystem::filesystem_error& error) {
            status(error.what());
        }
    }

private:
    class surface final : public bgui::linear {
    public:
        surface() : bgui::linear(bgui::orientation::vertical) {}
        void on_update() override {
            before_update();
            bgui::linear::on_update();
        }
        std::function<void()> before_update;
    };

    bool attached() const {
        if (!m_dock) return false;
        for (const auto& [layer, elements] : m_dock->get_elements())
            for (const auto& element : elements)
                if (element.get() == m_window) return true;
        return false;
    }

    std::string display_name(const std::filesystem::path& path) const {
        const auto relative = path.lexically_relative(m_root);
        return relative.empty() || !scene_files::inside(path, m_root)
            ? path.string() : relative.generic_string();
    }

    void confirm_delete(const std::filesystem::path& path) {
        if (m_delete_modal) return;
        auto& dialog = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
        m_delete_modal = &dialog;
        dialog.add<bgui::text>("Excluir definitivamente “" + display_name(path) + "”?", .32f);
        dialog.set_confirmation_text(tr("Excluir definitivamente"));
        dialog.set_on_confirm([this, path] {
            m_delete_modal = nullptr;
            m_erase(path);
        });
        dialog.add<bgui::button>(tr("Cancelar"), .33f, [this, pointer = &dialog] {
            m_delete_modal = nullptr;
            bgui::add_function([pointer] { bgui::get_layout().remove(pointer); });
        });
    }

    std::function<void()> m_visual;
    open_action m_open;
    file_action m_create;
    open_action m_erase;
    std::function<void(scenes_view&)> m_sync;
    bgui::dock* m_dock{};
    bgui::window* m_window{};
    bgui::modal* m_delete_modal{};
    std::string m_project;
    bgui::text* m_active_label{};
    bgui::inputbox* m_name{};
    bgui::text* m_status{};
    bgui::linear* m_list{};
    std::filesystem::path m_root;
    std::filesystem::path m_active;
    bool m_manifest{false};
    bool m_refresh{true};
};
}
