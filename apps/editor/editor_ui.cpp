#include "editor_ui.hpp"

#include <elem/button.hpp>
#include <elem/icon.hpp>
#include <elem/menu_bar.hpp>

#include <iostream>
#include <string>

namespace editor::ui {
namespace {

constexpr const char* ui_configuration_path = "editor-ui.cfg";
constexpr const char* project_path = "project.bengine";

// Glifos do ícone (Font Awesome: play / stop / pause)
constexpr const char* icon_play = "\xef\x81\x8b";
constexpr const char* icon_stop = "\xef\x81\x8d";
constexpr const char* icon_pause = "\xef\x81\x8c";

// Cores no estilo Unity
const bgui::color play_normal   {0.220f, 0.220f, 0.220f, 1.f}; // #383838
const bgui::color play_hover    {0.310f, 0.310f, 0.310f, 1.f}; // #4F4F4F
const bgui::color play_pressed  {0.160f, 0.160f, 0.160f, 1.f};
const bgui::color playing_normal{0.170f, 0.360f, 0.580f, 1.f}; // azul "em execução"
const bgui::color playing_hover {0.230f, 0.440f, 0.680f, 1.f};
const bgui::color playing_press {0.120f, 0.280f, 0.470f, 1.f};
const bgui::color icon_color    {0.85f,  0.85f,  0.85f, 1.f};
const bgui::color icon_color_on {1.f,    1.f,    1.f,   1.f};

void add_flexible_spacer(bgui::menu_bar& menu_bar) {
    auto& spacer = menu_bar.add<bgui::linear>();
    spacer.style.layout.require_mode(
        bgui::mode::stretch,
        bgui::mode::match_parent
    );
}

void apply_play_visual(
    bgui::button& button,
    bgui::icon& icon,
    const bool playing
) {
    auto& bg = button.style.visual.background;
    bg.normal  = playing ? playing_normal : play_normal;
    bg.hover   = playing ? playing_hover  : play_hover;
    bg.pressed = playing ? playing_press  : play_pressed;

    icon.style.visual.background.normal = playing ? icon_color_on : icon_color;
    icon.set_buffer(playing ? icon_stop : icon_play);
}

void apply_pause_visual(
    bgui::button& button,
    bgui::icon& icon,
    const bool paused
) {
    auto& bg = button.style.visual.background;
    bg.normal  = paused ? playing_normal : play_normal;
    bg.hover   = paused ? playing_hover  : play_hover;
    bg.pressed = paused ? playing_press  : play_pressed;

    icon.style.visual.background.normal = paused ? icon_color_on : icon_color;
    icon.set_buffer(icon_pause);
}

class runtime_play_button final : public bgui::button {
public:
    runtime_play_button(const std::function<void()>& on_click)
        : bgui::button("", 0.3f, on_click) {}

    std::function<void()> on_frame;

    void on_update() override {
        if (on_frame)
            on_frame();
        bgui::button::on_update();
    }
};

void report_result(
    const char* action,
    const bool success,
    const std::string& error
) {
    if (success)
        std::clog << "[editor] " << action << " completed.\n";
    else
        std::cerr << "[editor] " << action << " failed: " << error << '\n';
}

}

editor_ui::editor_ui(
    bgui::dock& dock,
    bgui::menu_bar& menu_bar,
    ui_persistence_system& ui_persistence,
    project_persistence_system& project_persistence
)
    : m_dock(dock),
      m_project_persistence(project_persistence) {
    menu_bar.add_menu(
        "Files",
        0.3f,
        [this, ui_persistence_ptr = &ui_persistence](bgui::context_menu& menu) {
            menu.add_button("Save Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.save(project_path, error);
                report_result("Saving project", success, error);
            });
            menu.add_button("Load Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.load(project_path, error);
                report_result("Loading project", success, error);
                if (success && m_project_loaded_callback)
                    m_project_loaded_callback();
            });
            menu.add_separator();
            menu.add_button("Save UI", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->save(ui_configuration_path, error);
                report_result("Saving UI", success, error);
            });
            menu.add_button("Load UI", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->load(ui_configuration_path, error);
                report_result("Loading UI", success, error);
            });
        }
    );
    add_flexible_spacer(menu_bar);

    auto& play_container = menu_bar.add<bgui::linear>();
    play_container.style.layout.require_mode(
        bgui::mode::wrap_content,
        bgui::mode::match_parent
    );
    play_container.style.layout.align = bgui::vec<2, bgui::alignment>{
        bgui::alignment::center,
        bgui::alignment::center
    };
    play_container.style.layout.padding = bgui::vec4i{4, 0, 4, 0};
    auto configure_control = [](bgui::button& button, bgui::icon& icon) {
        button.add_class("runtime-control-button");
        button.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::pixel
        );
        button.style.layout.require_width(bgui::mode::pixel, 36.f);
        button.style.layout.require_height(bgui::mode::pixel, 22.f);
        button.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };
        button.style.visual.border_radius = 3.f;
        button.get_label().set_enable(false);

        icon.receives_input(false);
        icon.style.layout.require_mode(
            bgui::mode::wrap_content,
            bgui::mode::wrap_content
        );
        icon.style.layout.align = bgui::vec<2, bgui::alignment>{
            bgui::alignment::center,
            bgui::alignment::center
        };
    };

    m_play_button = &play_container.add<runtime_play_button>(
        [this]() {
            const bool should_play = !m_is_playing;
            std::string error;
            if (m_play_state_callback &&
                !m_play_state_callback(should_play, error)) {
                report_result(
                    should_play ? "Starting runtime" : "Stopping runtime",
                    false,
                    error
                );
                return;
            }

            m_is_playing = should_play;
            m_is_paused = false;
            apply_play_visual(*m_play_button, *m_play_icon, m_is_playing);
            apply_pause_visual(*m_pause_button, *m_pause_icon, false);
            m_pause_button->set_enable(m_is_playing);
        }
    );
    m_play_icon = &m_play_button->add<bgui::icon>("play", 0.28f);
    configure_control(*m_play_button, *m_play_icon);
    m_play_button->add_class("runtime-play-button");
    apply_play_visual(*m_play_button, *m_play_icon, false);

    m_pause_button = &play_container.add<bgui::button>(
        "",
        0.3f,
        [this]() {
            if (!m_is_playing || !m_runtime_pause_callback)
                return;

            const bool should_pause = !m_is_paused;
            std::string error;
            if (!m_runtime_pause_callback(should_pause, error)) {
                report_result(
                    should_pause ? "Pausing runtime" : "Resuming runtime",
                    false,
                    error
                );
                return;
            }

            m_is_paused = should_pause;
            apply_pause_visual(*m_pause_button, *m_pause_icon, m_is_paused);
        }
    );
    m_pause_icon = &m_pause_button->add<bgui::icon>("pause", 0.28f);
    configure_control(*m_pause_button, *m_pause_icon);
    m_pause_button->add_class("runtime-pause-button");
    m_pause_button->set_enable(false);
    apply_pause_visual(*m_pause_button, *m_pause_icon, false);

    static_cast<runtime_play_button*>(m_play_button)->on_frame = [this]() {
        if (!m_is_playing || !m_runtime_running_callback)
            return;

        std::string error;
        if (m_runtime_running_callback(error)) {
            if (!error.empty())
                report_result("Checking runtime", false, error);
            return;
        }
        if (m_play_state_callback &&
            !m_play_state_callback(false, error)) {
            report_result("Stopping runtime", false, error);
        }
        m_is_playing = false;
        m_is_paused = false;
        m_pause_button->set_enable(false);
        apply_play_visual(*m_play_button, *m_play_icon, false);
        apply_pause_visual(*m_pause_button, *m_pause_icon, false);
    };
    add_flexible_spacer(menu_bar);
}

void editor_ui::set_project_loaded_callback(std::function<void()> callback) {
    m_project_loaded_callback = std::move(callback);
}

void editor_ui::set_play_state_callback(
    std::function<bool(bool, std::string&)> callback
) {
    m_play_state_callback = std::move(callback);
}

void editor_ui::set_runtime_running_callback(
    std::function<bool(std::string&)> callback
) {
    m_runtime_running_callback = std::move(callback);
}

void editor_ui::set_runtime_pause_callback(
    std::function<bool(bool, std::string&)> callback
) {
    m_runtime_pause_callback = std::move(callback);
}

void editor_ui::update() {
    for (auto& panel : m_panels)
        if (panel)
            panel->update();
}

}