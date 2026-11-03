#pragma once
#include "context_action_button.hpp"
#include "project_catalog.hpp"
#include "../../editor/core/distribution_paths.hpp"

namespace editor::ui {
class project_delete_confirmation final : public bgui::modal {
    std::weak_ptr<bool> m_owner;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_closing{false};
public:
    explicit project_delete_confirmation(std::weak_ptr<bool> owner) : m_owner(std::move(owner)) {}
    void on_update() override {
        if (m_owner.expired()) {
            if (!m_closing) {
                m_closing = true;
                set_enable(false);
                std::weak_ptr<bool> alive = m_alive;
                bgui::add_function([this, alive]() { if (!alive.expired()) bgui::get_layout().remove(this); });
            }
            return;
        }
        bgui::modal::on_update();
    }
};

inline void configure_project_context_menu(context_action_button& button,
    std::filesystem::path project, std::function<std::filesystem::path()> active,
    std::function<void()> open, std::function<void()> removed,
    std::function<void(const std::string&)> report_error) {
    project = std::filesystem::absolute(project).lexically_normal();
    const auto alive = button.lifetime();
    button.context_options = [project, active, open, removed, report_error, alive]() {
        return context_actions{
            {"Abrir projeto", open},
            {"Remover da lista", [project, removed, report_error]() {
                try { forget_project(editor::distribution::user_path("editor-recent-projects.cfg"), project); removed(); }
                catch (const std::exception& e) { report_error(e.what()); }
            }},
            {"Apagar pasta do disco...", [project, active, removed, report_error, alive]() {
                auto& confirmation = bgui::get_layout().add<project_delete_confirmation, bgui::layer::overlay>(alive);
                confirmation.add<bgui::text>("Apagar definitivamente a pasta do projeto?", .34f);
                confirmation.add<bgui::text>(project.parent_path().string(), .28f);
                confirmation.add<bgui::text>("Todos os arquivos da pasta serão apagados. Esta ação não pode ser desfeita.", .28f);
                confirmation.set_confirmation_text("Apagar pasta definitivamente");
                confirmation.set_on_confirm([project, active, removed, report_error, alive]() {
                    if (alive.expired()) return;
                    try { erase_project(project, active()); forget_project(editor::distribution::user_path("editor-recent-projects.cfg"), project); removed(); }
                    catch (const std::exception& e) { report_error(e.what()); }
                });
                confirmation.add<bgui::button>("Cancelar", .3f, [surface = &confirmation]() {
                    bgui::add_function([surface]() { bgui::get_layout().remove(surface); });
                });
            }}
        };
    };
}
}
