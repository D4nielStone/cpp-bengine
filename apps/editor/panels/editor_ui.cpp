#include "editor_ui.hpp"
#include "native_file_dialog.hpp"
#include "project_creation_system.hpp"
#include "ui_scale_system.hpp"

#include <elem/checkbox.hpp>

#include <components/directional_light.hpp>

#include <elem/button.hpp>
#include <elem/icon.hpp>
#include <elem/inputbox.hpp>
#include <elem/menu_bar.hpp>
#include <elem/modal.hpp>
#include <elem/progress_bar.hpp>
#include <elem/text.hpp>
#include <elem/vector_field.hpp>
#include <lay/linear.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>

namespace editor::ui {
namespace {

constexpr const char* ui_configuration_path = "editor-ui.cfg";
constexpr const char* recent_projects_path = "editor-recent-projects.cfg";

void remember_project(const std::string& path) {
    std::vector<std::string> projects{path};
    std::ifstream input(recent_projects_path);
    for (std::string previous; std::getline(input, previous);) {
        if (!previous.empty() && previous != path && projects.size() < 12)
            projects.push_back(previous);
    }
    std::ofstream output(recent_projects_path, std::ios::trunc);
    for (const auto& project : projects)
        output << project << '\n';
}

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

class preferences_content final : public bgui::linear {
public:
    preferences_content() : bgui::linear(bgui::orientation::vertical) {
        set_scrollable(true);
    }

    void on_update() override {
        const auto viewport = bgui::get_context_size();
        const float scale = bgui::get_global_scale();
        const bgui::vec2 size{
            std::clamp(viewport.x / scale - 64.f, 160.f, 420.f),
            std::clamp(viewport.y / scale - 120.f, 80.f, 380.f)
        };
        if (size.x != m_size.x || size.y != m_size.y) {
            style.layout.require_size(size.x, size.y);
            mark_style_dirty();
            m_size = size;
        }
        bgui::linear::on_update();
    }

private:
    bgui::vec2 m_size{0.f, 0.f};
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

std::string format_number(const float value) {
    char buffer[64];
    const auto result = std::to_chars(
        buffer,
        buffer + sizeof(buffer),
        value,
        std::chars_format::general,
        6
    );
    if (result.ec == std::errc{})
        return {buffer, result.ptr};
    return std::to_string(value);
}

bool parse_number(const std::string& buffer, float& value) {
    const auto result = std::from_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value,
        std::chars_format::general
    );
    return result.ec == std::errc{} &&
        result.ptr == buffer.data() + buffer.size() &&
        std::isfinite(value);
}

std::shared_ptr<commons::directional_light> global_directional_light(
    const std::shared_ptr<commons::ecs>& registry
) {
    if (!registry)
        return nullptr;

    std::shared_ptr<commons::directional_light> result;
    registry->cada<commons::directional_light>(
        [&registry, &result](const std::uint32_t entity) {
            if ((registry->get_components(entity) & commons::component::COMPONENTE_CAM) != commons::component::COMPONENTE_NONE)
                return;
            result = registry->get<commons::directional_light>(entity);
        }
    );
    return result;
}

}

editor_ui::editor_ui(
    bgui::dock& dock,
    bgui::menu_bar& menu_bar,
    ui_persistence_system& ui_persistence,
    project_persistence_system& project_persistence,
    std::shared_ptr<commons::ecs> registry,
    std::string project_path,
    std::filesystem::path build_directory
)
    : m_dock(dock),
      m_project_persistence(project_persistence),
      m_registry(std::move(registry)),
      m_project_path(std::move(project_path)),
      m_build_directory(std::move(build_directory)),
      m_repository_root(m_build_directory.parent_path()),
      m_build_process(std::make_unique<build_process>()) {
    create_project_modal(m_project_path, "");

    menu_bar.add_menu(
        "File",
        0.3f,
        [this](bgui::context_menu& menu) {
            menu.add_button("New Project...", [this]() {
                create_new_project_modal();
                m_new_project_modal->set_enable(true);
            });
            menu.add_button("Save Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.save(m_project_path, error);
                report_result("Saving project", success, error);
            });
            menu.add_button("Project Manager", [this]() {
                if (!m_project_modal)
                    create_project_modal(m_project_path, "");

                if (m_project_modal)
                    m_project_modal->set_enable(true);
            });
            menu.add_separator();
            menu.add_button("Exit", [this]() { confirm_exit(); });
        }
    );
    menu_bar.add_menu(
        "Build",
        0.3f,
        [this](bgui::context_menu& menu) {
            menu.add_button(
                "Build Runtime",
                    [this]() { open_build_modal(); }
            );
        }
    );
    menu_bar.add_menu(
        "Edit",
        0.3f,
        [this](bgui::context_menu& menu) {
            menu.add_button("Undo", [this]() {
                if (m_undo_callback)
                    m_undo_callback();
            });
            menu.add_button("Redo", [this]() {
                if (m_redo_callback)
                    m_redo_callback();
            });
        }
    );
    menu_bar.add_menu(
        "View", 0.3f,
        [this, ui_persistence_ptr = &ui_persistence](bgui::context_menu& menu) {
            menu.add_button("Zoom In     Ctrl +", []() { change_ui_scale(1); });
            menu.add_button("Zoom Out    Ctrl -", []() { change_ui_scale(-1); });
            menu.add_button("Actual Size (100%)    Ctrl 0", []() { reset_ui_scale(); });
            menu.add_separator();
            menu.add_button("Layout: Standard", [this]() { apply_layout(false); });
            menu.add_button("Layout: Wide Scene", [this]() { apply_layout(true); });
            menu.add_button("Reset UI", [this]() {
                reset_ui_scale();
                apply_layout(false);
            });
            menu.add_button("Save Layout", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->save(ui_configuration_path, error);
                report_result("Saving UI", success, error);
            });
            menu.add_button("Load Layout", [ui_persistence_ptr]() {
                std::string error;
                const bool success = ui_persistence_ptr->load(ui_configuration_path, error);
                report_result("Loading UI", success, error);
            });
        }
    );
    menu_bar.add_menu(
        "Settings",
        0.3f,
        [this](bgui::context_menu& menu) {
            menu.add_button("Editor Preferences...", [this]() { open_preferences_modal(); });
            menu.add_button(
                "Global Lighting",
                [this]() { open_global_lighting_modal(); }
            );
            menu.add_button(
                "Global Physics",
                [this]() { open_global_physics_modal(); }
            );
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
        update_build_process();
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

void editor_ui::create_build_modal() {
    if (m_build_modal) {
        m_build_modal->set_enable(true);
        return;
    }

    m_build_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_build_modal->content().add<bgui::text>(
        "Choose Linux or Windows x64. Downloads the Zig compiler on first use "
        "and prepares target libraries with vcpkg. Windows hosts require WSL2.",
        0.3f
    );
    m_build_modal->content().add<bgui::text>(
        "Build output directory",
        0.3f
    );
    m_build_directory_input = &m_build_modal->content().add<bgui::inputbox>(
        m_build_directory.string(),
        "Choose a build directory...",
        0.28f
    );
    m_build_directory_input->m_enter_func = [this](const std::string& buffer) {
        m_build_directory = buffer;
        m_repository_root = m_build_directory.parent_path();
    };
    auto& directory_actions = m_build_modal->content().add<bgui::linear>(
        bgui::orientation::horizontal
    );
    directory_actions.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    m_build_directory_browse_button = &directory_actions.add<bgui::button>(
        "Browse",
        0.28f,
        [this]() {
            create_build_directory_modal();
            m_repository_root = m_build_directory.parent_path();
        }
    );
    m_build_modal->content().add<bgui::text>(
        "Artifacts are written under <directory>/platform-builds/<target>/apps.",
        0.25f
    );
    const auto add_target = [this](const char* icon_name,
                                   const char* label,
                                   const char* requirements,
                                   const build_target target) {
        auto& row = m_build_modal->content().add<bgui::linear>(
            bgui::orientation::horizontal
        );
        row.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        auto& icon = row.add<bgui::icon>(icon_name, 0.3f);
        icon.style.layout.require_mode(
            bgui::mode::pixel,
            bgui::mode::pixel
        );
        icon.style.layout.require_width(bgui::mode::pixel, 28.f);
        icon.style.layout.require_height(bgui::mode::pixel, 28.f);
        row.add<bgui::text>(label, 0.3f);
        row.add<bgui::button>(
            target == build_target::windows
                ? "Download Windows Toolchain & Build"
                : "Download Linux Toolchain & Build",
            0.28f,
            [this, target]() { start_platform_build(target); }
        );
        m_build_modal->content().add<bgui::text>(requirements, 0.25f);
    };
    add_target(
        "linux",
        "Linux x64",
        "Requires Linux OpenGL/GLFW/X11 development packages; Windows hosts use WSL2.",
        build_target::linux
    );
    add_target(
        "windows",
        "Windows x64 (Zig/MinGW)",
        "Downloads Zig, PowerShell, and MinGW windres; vcpkg prepares libraries. Linux hosts need apt-get/dpkg-deb.",
        build_target::windows
    );
    m_build_status = &m_build_modal->content().add<bgui::text>(
        "No build started.",
        0.28f
    );
    m_build_progress = &m_build_modal->content().add<bgui::progress_bar>(
        0.f,
        100.f,
        0.f
    );
    m_build_progress->style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::pixel
    );
    m_build_progress->style.layout.require_height(bgui::mode::pixel, 12.f);
    m_build_modal->set_confirmation_text("Close");
    m_build_modal->set_on_confirm([this]() {
        m_build_modal = nullptr;
        m_build_status = nullptr;
        m_build_progress = nullptr;
        m_build_directory_input = nullptr;
        m_build_directory_browse_button = nullptr;
    });
    if (m_build_running) {
        m_build_directory_input->set_enable(false);
        m_build_directory_browse_button->set_enable(false);
    }
}

void editor_ui::open_build_modal() {
    create_build_modal();
}

void editor_ui::start_platform_build(const build_target target) {
    create_build_modal();
    if (m_build_running) {
        if (m_build_status)
            m_build_status->set_buffer("A platform build is already running.");
        return;
    }
    if (m_is_playing) {
        if (m_build_status)
            m_build_status->set_buffer("Stop Play mode before building.");
        return;
    }

    std::string error;
    if (!m_build_directory_input ||
        m_build_directory_input->get_buffer().empty()) {
        error = "Choose a build output directory.";
    } else {
        std::error_code path_error;
        auto selected_directory = std::filesystem::absolute(
            m_build_directory_input->get_buffer(),
            path_error
        ).lexically_normal();
        if (path_error) {
            error = "Could not resolve build directory: " +
                path_error.message();
        } else {
            const bool path_exists = std::filesystem::exists(
                selected_directory,
                path_error
            );
            if (path_error) {
                error = "Could not inspect build directory: " +
                    path_error.message();
            } else if (path_exists &&
                       !std::filesystem::is_directory(
                           selected_directory,
                           path_error
                       ) &&
                       !path_error) {
                error = "The selected build path is not a directory.";
            } else if (path_error) {
                error = "Could not inspect build directory: " +
                    path_error.message();
            } else {
                m_build_directory = selected_directory;
                m_build_directory_input->set_buffer(
                    m_build_directory.string()
                );
            }
        }
    }
    if (error.empty() &&
        (m_build_directory.empty() || m_repository_root.empty())) {
        error = "Could not locate the source or build directory.";
    }
    if (!error.empty() ||
        !m_build_process->start(
            m_repository_root,
            m_build_directory,
            target,
            error
        )) {
        if (error.empty())
            error = "Could not locate the source or build directory.";
        if (m_build_status)
            m_build_status->set_buffer("Build could not start: " + error);
        report_result("Building platform target", false, error);
        return;
    }

    m_build_running = true;
    m_build_directory_input->set_enable(false);
    m_build_directory_browse_button->set_enable(false);
    if (m_build_status)
        m_build_status->set_buffer(
            std::string("Preparing ") +
            (target == build_target::windows ? "Windows" : "Linux") +
            " x64 toolchain..."
        );
    if (m_build_progress)
        m_build_progress->set_value(0.f);
    std::clog << "[editor] Platform build started; output: "
              << m_build_process->log_path().string() << '\n';
}

void editor_ui::create_build_directory_modal() {
    std::string error;
    const auto selected = editor::browse_native(
        m_build_directory_input->get_buffer(), true, error);
    if (!selected.empty()) {
        m_build_directory = selected;
        m_build_directory_input->set_buffer(selected.string());
    }
    if (!error.empty() && m_build_status)
        m_build_status->set_buffer(error);
}

void editor_ui::update_build_process() {
    if (!m_build_running)
        return;

    std::string error;
    if (m_build_process->running(error)) {
        if (!error.empty()) {
            m_build_running = false;
            if (m_build_directory_input)
                m_build_directory_input->set_enable(true);
            if (m_build_directory_browse_button)
                m_build_directory_browse_button->set_enable(true);
            if (m_build_status)
                m_build_status->set_buffer("Build status unavailable: " + error);
            report_result("Checking build", false, error);
        }
        std::string status;
        const float progress = m_build_process->progress(status);
        if (m_build_status && !status.empty())
            m_build_status->set_buffer(status);
        if (m_build_progress)
            m_build_progress->set_value(progress);
        return;
    }

    m_build_running = false;
    if (m_build_directory_input)
        m_build_directory_input->set_enable(true);
    if (m_build_directory_browse_button)
        m_build_directory_browse_button->set_enable(true);
    const bool succeeded = m_build_process->exit_code() == 0;
    std::string final_status;
    const float final_progress = m_build_process->progress(final_status);
    if (succeeded && m_build_progress)
        m_build_progress->set_value(final_progress);
    if (m_build_status)
        m_build_status->set_buffer(
            succeeded
                ? final_status
                : "Build failed (exit code " +
                    std::to_string(m_build_process->exit_code()) +
                    "). Output: " + m_build_process->log_path().string()
        );
    report_result(
        "Building platform target",
        succeeded,
        succeeded
            ? std::string{}
            : "Platform build process exited with code " +
                std::to_string(m_build_process->exit_code()) +
                ". Build output: " + m_build_process->log_path().string()
    );
}

void editor_ui::set_history_callbacks(
    std::function<void()> undo_callback,
    std::function<void()> redo_callback
) {
    m_undo_callback = std::move(undo_callback);
    m_redo_callback = std::move(redo_callback);
}

void editor_ui::create_global_lighting_modal() {
    m_global_lighting_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_global_lighting_modal->content().add<bgui::text>(
        "Configure the scene's global directional light.",
        0.3f
    );

    auto add_vector = [this](
        const std::string& label,
        std::array<bgui::inputbox*, 3>& inputs
    ) {
        auto& field = m_global_lighting_modal->content().add<bgui::vector_field>(
            label,
            std::vector<std::string>{"X", "Y", "Z"},
            0.28f
        );
        for (std::size_t axis = 0; axis < inputs.size(); ++axis) {
            inputs[axis] = &field.get_component(axis);
            inputs[axis]->add_class("editor-input");
        }
    };
    add_vector("Ray direction", m_light_direction_inputs);
    add_vector("Ambient", m_light_ambient_inputs);
    add_vector("Color", m_light_color_inputs);

    auto& intensity = m_global_lighting_modal->content().add<bgui::vector_field>(
        "Intensity",
        std::vector<std::string>{"Value"},
        0.28f
    );
    m_light_intensity_input = &intensity.get_component(0);
    m_light_intensity_input->add_class("editor-input");
    m_global_lighting_modal->set_confirmation_text("Apply");
    m_global_lighting_modal->set_on_confirm([this]() {
        auto light = global_directional_light(m_registry);
        std::array<float, 3> direction{};
        std::array<float, 3> ambient{};
        std::array<float, 3> color{};
        float intensity_value{};
        const auto read_vector = [](const auto& inputs, auto& values) {
            for (std::size_t axis = 0; axis < inputs.size(); ++axis)
                if (!inputs[axis] ||
                    !parse_number(inputs[axis]->get_buffer(), values[axis]))
                    return false;
            return true;
        };
        if (!read_vector(m_light_direction_inputs, direction) ||
            !read_vector(m_light_ambient_inputs, ambient) ||
            !read_vector(m_light_color_inputs, color) ||
            !m_light_intensity_input ||
            !parse_number(
                m_light_intensity_input->get_buffer(),
                intensity_value
            )) {
            report_result(
                "Applying global lighting settings",
                false,
                "Enter a valid finite number in every field."
            );
        } else if (!m_registry) {
            report_result(
                "Applying global lighting settings",
                false,
                "The scene registry is unavailable."
            );
        } else {
            if (!light) {
                auto entity = m_registry->create();
                m_registry->add<commons::directional_light>(
                    entity,
                    bgui::vec3{-0.2f, -1.f, -0.3f},
                    bgui::vec3{0.15f},
                    bgui::vec3{1.f},
                    1.f
                );
                light = m_registry->get<commons::directional_light>(entity.id);
            }
            light->direction = {direction[0], direction[1], direction[2]};
            light->ambient = {ambient[0], ambient[1], ambient[2]};
            light->color = {color[0], color[1], color[2]};
            light->intensity = intensity_value;
            report_result("Applying global lighting settings", true, {});
        }

        m_global_lighting_modal = nullptr;
        m_light_direction_inputs = {};
        m_light_ambient_inputs = {};
        m_light_color_inputs = {};
        m_light_intensity_input = nullptr;
    });
}

void editor_ui::open_global_lighting_modal() {
    create_global_lighting_modal();

    const auto light = global_directional_light(m_registry);
    const commons::directional_light defaults{
        {-0.2f, -1.f, -0.3f},
        bgui::vec3{0.15f},
        bgui::vec3{1.f},
        1.f
    };
    const auto& values = light ? *light : defaults;
    const auto set_vector = [](const auto& inputs, const bgui::vec3& vector) {
        for (std::size_t axis = 0; axis < inputs.size(); ++axis)
            inputs[axis]->set_buffer(format_number(vector[axis]));
    };
    set_vector(m_light_direction_inputs, values.direction);
    set_vector(m_light_ambient_inputs, values.ambient);
    set_vector(m_light_color_inputs, values.color);
    m_light_intensity_input->set_buffer(format_number(values.intensity));
    m_global_lighting_modal->set_enable(true);
}

void editor_ui::create_global_physics_modal() {
    m_global_physics_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_global_physics_modal->content().add<bgui::text>(
        "Configure global gravity for this project.",
        0.3f
    );
    auto& gravity = m_global_physics_modal->content().add<bgui::vector_field>(
        "Gravity",
        std::vector<std::string>{"X", "Y", "Z"},
        0.28f
    );
    for (std::size_t axis = 0; axis < m_gravity_inputs.size(); ++axis) {
        m_gravity_inputs[axis] = &gravity.get_component(axis);
        m_gravity_inputs[axis]->add_class("editor-input");
    }
    m_global_physics_modal->set_confirmation_text("Apply");
    m_global_physics_modal->set_on_confirm([this]() {
        std::array<float, 3> gravity_value{};
        bool valid = true;
        for (std::size_t axis = 0; axis < m_gravity_inputs.size(); ++axis)
            valid = valid && m_gravity_inputs[axis] &&
                parse_number(
                    m_gravity_inputs[axis]->get_buffer(),
                    gravity_value[axis]
                );

        if (!valid) {
            report_result(
                "Applying global physics settings",
                false,
                "Enter a valid finite number in every gravity field."
            );
        } else {
            m_project_persistence.set_global_gravity(gravity_value);
            report_result("Applying global physics settings", true, {});
        }
        m_global_physics_modal = nullptr;
        m_gravity_inputs = {};
    });
}

void editor_ui::open_global_physics_modal() {
    create_global_physics_modal();
    const auto& gravity = m_project_persistence.global_gravity();
    for (std::size_t axis = 0; axis < m_gravity_inputs.size(); ++axis)
        m_gravity_inputs[axis]->set_buffer(format_number(gravity[axis]));
    m_global_physics_modal->set_enable(true);
}

void editor_ui::refresh_project_picker(const std::string& initial) {
    std::string error;
    const auto selected = editor::browse_native(initial, false, error);
    if (!selected.empty() && m_project_directory_input)
        m_project_directory_input->set_buffer(selected.string());
    if (!error.empty() && m_project_picker_status)
        m_project_picker_status->set_buffer(error);
}

bool editor_ui::load_project_selection() {
    if (!m_project_directory_input || !m_project_modal)
        return false;

    std::filesystem::path selected_path(
        m_project_directory_input->get_buffer()
    );
    if (selected_path.empty()) {
        m_project_picker_status->set_buffer("Select a project folder or file.");
        return false;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(selected_path, error)) {
        if (error) {
            m_project_picker_status->set_buffer(
                "Could not inspect selected path: " + error.message()
            );
            return false;
        }
    } else {
        const auto root = selected_path;
        std::filesystem::path main_scene;
        std::ifstream marker(root / ".bengine-main-scene");
        if (marker) {
            std::string marker_contents;
            std::getline(marker, marker_contents);
            if (!marker_contents.empty())
                main_scene = root / marker_contents;
        }
        if (marker.bad()) {
            m_project_picker_status->set_buffer(
                "Could not read the project's main-scene marker."
            );
            return false;
        }

        const std::vector<std::filesystem::path> candidates{
            root / "project.bengine",
            main_scene,
            root / "game.world",
            root / "main.scene"
        };
        selected_path.clear();
        for (const auto& candidate : candidates) {
            if (candidate.empty())
                continue;
            error.clear();
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                selected_path = candidate;
                break;
            }
        }
        if (selected_path.empty()) {
            m_project_picker_status->set_buffer(
                "No project file found. Select a .bengine, .world, or .scene file."
            );
            return false;
        }
    }

    selected_path = std::filesystem::absolute(selected_path, error).lexically_normal();
    if (error) {
        m_project_picker_status->set_buffer(
            "Could not resolve selected project: " + error.message()
        );
        return false;
    }

    std::string load_error;
    if (!m_project_persistence.load(selected_path.string(), load_error)) {
        report_result("Loading project", false, load_error);
        m_project_picker_status->set_buffer(load_error);
        return false;
    }

    m_project_path = selected_path.string();
    remember_project(m_project_path);
    report_result("Loading project", true, {});
    if (m_project_loaded_callback)
        m_project_loaded_callback(m_project_path);
    m_project_modal->set_enable(false);
    return true;
}

void editor_ui::create_project_modal(
    const std::string& selected_path,
    const std::string& status
) {
    m_project_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_project_modal->set_enable(false);
    m_project_modal->content().add<bgui::text>("Project Manager", 0.46f);
    m_project_modal->content().add<bgui::text>("Recent projects", 0.32f);
    std::ifstream recent_input(recent_projects_path);
    std::size_t recent_count = 0;
    for (std::string recent; std::getline(recent_input, recent);) {
        std::error_code recent_error;
        if (recent.empty() ||
            !std::filesystem::is_regular_file(recent, recent_error) || recent_error)
            continue;
        const auto recent_path = std::filesystem::path(recent);
        auto& recent_button = m_project_modal->content().add<bgui::button>(
            recent_path.parent_path().filename().string() + "  —  " + recent,
            0.28f,
            [this, recent]() {
                if (m_project_directory_input)
                    m_project_directory_input->set_buffer(recent);
                load_project_selection();
            }
        );
        recent_button.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
        ++recent_count;
        if (recent_count == 6)
            break;
    }
    if (recent_count == 0)
        m_project_modal->content().add<bgui::text>("No recent projects", 0.26f);
    m_project_modal->content().add<bgui::text>(
        "Open an existing project",
        0.34f
    );
    m_project_directory_input = &m_project_modal->content().add<bgui::inputbox>(
        selected_path,
        "Project directory or file...",
        0.32f
    );
    auto& path_actions = m_project_modal->content().add<bgui::linear>(
        bgui::orientation::horizontal
    );
    path_actions.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    path_actions.add<bgui::button>(
        "Browse",
        0.3f,
        [this]() {
            if (m_project_directory_input)
                refresh_project_picker(m_project_directory_input->get_buffer());
        }
    );
    path_actions.add<bgui::button>("New Project...", 0.3f, [this]() {
        create_new_project_modal();
        if (m_new_project_modal)
            m_new_project_modal->set_enable(true);
    });
    m_project_picker_status = &m_project_modal->content().add<bgui::text>(
        status,
        0.28f
    );
    m_project_modal->set_confirmation_text("Open");
    m_project_modal->set_on_confirm([this]() {
        const auto selected_path = m_project_directory_input
            ? m_project_directory_input->get_buffer()
            : std::string{};
        const bool loaded = load_project_selection();
        if (loaded) {
            m_project_modal = nullptr;
            m_project_directory_input = nullptr;
            m_project_picker_status = nullptr;
            return;
        }

        const auto status = m_project_picker_status
            ? m_project_picker_status->get_buffer()
            : std::string{"Could not load the selected project."};
        create_project_modal(selected_path, status);
        if (m_project_directory_input)
            m_project_directory_input->set_buffer(selected_path);
        if (m_project_picker_status)
            m_project_picker_status->set_buffer(status);
        m_project_modal->set_enable(true);
    });
}

void editor_ui::create_new_project_modal() {
    if (m_new_project_modal)
        return;
    m_new_project_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_new_project_modal->set_enable(false);
    auto& content = m_new_project_modal->content();
    content.add<bgui::text>("Create a project", 0.4f);
    content.add<bgui::text>(
        "A project folder with the starter scene and asset folders will be created.",
        0.28f
    );
    content.add<bgui::text>("Project name", 0.28f);
    m_new_project_name_input = &content.add<bgui::inputbox>(
        "New Project", "Project name", 0.32f
    );
    content.add<bgui::text>("Location", 0.28f);
    const auto default_location = std::filesystem::path(m_project_path).parent_path();
    m_new_project_location_input = &content.add<bgui::inputbox>(
        default_location.empty()
            ? std::filesystem::current_path().string()
            : default_location.string(),
        "Parent folder", 0.32f
    );
    auto& status = content.add<bgui::text>("", 0.28f);
    auto& actions = content.add<bgui::linear>(bgui::orientation::horizontal);
    actions.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    actions.add<bgui::button>("Browse...", 0.3f, [this, &status]() {
        std::string error;
        const auto selected = editor::browse_native(
            m_new_project_location_input->get_buffer(), true, error);
        if (!selected.empty())
            m_new_project_location_input->set_buffer(selected.string());
        if (!error.empty())
            status.set_buffer(error);
    });
    m_new_project_modal->set_confirmation_text("Create Project");
    m_new_project_modal->set_on_confirm([this, &status]() {
        const auto name = m_new_project_name_input->get_buffer();
        const auto first = name.find_first_not_of(" \t\r\n");
        const std::filesystem::path name_path(name);
        if (first == std::string::npos || name_path.has_parent_path() ||
            name == "." || name == "..") {
            status.set_buffer("Enter a project name without folder separators.");
            return;
        }
        if (m_new_project_location_input->get_buffer().empty()) {
            status.set_buffer("Choose a parent folder for the new project.");
            return;
        }
        const auto folder = std::filesystem::path(
            m_new_project_location_input->get_buffer()
        ) / name;
        std::string error;
        if (!create_project_files(folder, error)) {
            status.set_buffer(error);
            return;
        }
        const auto project_file = project_file_path(folder).string();
        if (!m_project_persistence.load(project_file, error)) {
            status.set_buffer(error);
            return;
        }
        m_project_path = project_file;
        remember_project(m_project_path);
        if (m_project_loaded_callback)
            m_project_loaded_callback(m_project_path);
        m_new_project_modal->set_enable(false);
        if (m_project_modal)
            m_project_modal->set_enable(false);
        report_result("Creating project", true, {});
    });
}

void editor_ui::create_exit_modal() {
    m_exit_modal = &bgui::get_layout().add<
        bgui::modal,
        bgui::layer::overlay
    >();
    m_exit_modal->content().add<bgui::text>(
        "Save changes before closing the editor?",
        0.34f
    );
    m_exit_modal->set_confirmation_text("Save and Exit");
    m_exit_modal->set_on_confirm([this]() {
        std::string error;
        if (!m_project_persistence.save(m_project_path, error)) {
            report_result("Saving project before exit", false, error);
            m_exit_modal = nullptr;
            create_exit_modal();
            return;
        }

        report_result("Saving project before exit", true, {});
        m_exit_modal = nullptr;
        if (m_exit_callback)
            m_exit_callback();
    });
    auto& exit_actions = m_exit_modal->content().add<bgui::linear>(
        bgui::orientation::horizontal
    );
    exit_actions.style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::wrap_content
    );
    exit_actions.add<bgui::button>("Discard and Exit", 0.3f, [this]() {
        if (m_exit_modal)
            m_exit_modal->set_enable(false);
        if (m_exit_callback)
            m_exit_callback();
    });
    exit_actions.add<bgui::button>("Cancel", 0.3f, [this]() {
        if (m_exit_modal)
            m_exit_modal->set_enable(false);
    });
}

void editor_ui::set_project_loaded_callback(
    std::function<void(const std::string&)> callback
) {
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

void editor_ui::set_editor_context(std::shared_ptr<editor_context> context) {
    m_context = std::move(context);
}

void editor_ui::apply_layout(bool wide) {
    auto configuration = m_dock.get_configuration();
    configuration.splits.clear();
    configuration.tab_groups.clear();
    configuration.top_ratio = .24f;
    configuration.left_ratio = wide ? .16f : .24f;
    configuration.right_ratio = wide ? .18f : .24f;
    configuration.bottom_ratio = wide ? .18f : .24f;
    for (auto& window : configuration.windows) {
        window.floating = false;
        window.weight = 1.f;
        if (window.title == "Scene")
            window.area = bgui::dock_area::center;
        else if (window.title == "Hierarchy")
            window.area = bgui::dock_area::left;
        else if (window.title == "Arquivos" || window.title == "Debug")
            window.area = bgui::dock_area::bottom;
        else if (window.title == "Inspector" || window.title == "Editor Diagnostics")
            window.area = bgui::dock_area::right;
    }
    configuration.tab_groups.push_back({"Arquivos", "Arquivos", {"Arquivos", "Debug"}});
    m_dock.apply_configuration(configuration);
}

void editor_ui::open_preferences_modal() {
    if (!m_context)
        return;
    auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
    auto& content = modal.content().add<preferences_content>();
    content.add<bgui::text>("Editor Preferences", .4f);
    content.add<bgui::text>("Preferences apply to this editor session.", .26f);
    auto& grid = content.add<bgui::checkbox>(
        "Show scene grid", .3f, m_context->preferences.show_grid);
    constexpr std::array labels{
        "Movement speed (0.1 - 100)", "Sprint multiplier (1 - 10)",
        "Mouse sensitivity (0.01 - 2)", "Zoom sensitivity (0.01 - 10)",
        "Camera smoothing (0.1 - 100)"
    };
    const auto values = [](const editor_preferences& preferences) {
        return std::array{preferences.movement_speed, preferences.sprint_multiplier,
            preferences.mouse_sensitivity, preferences.zoom_sensitivity,
            preferences.smoothing_speed};
    };
    std::array<bgui::inputbox*, 5> inputs{};
    const auto current = values(m_context->preferences);
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        content.add<bgui::text>(labels[i], .28f);
        inputs[i] = &content.add<bgui::inputbox>(format_number(current[i]), "", .3f);
    }
    auto& status = content.add<bgui::text>("", .27f);
    content.add<bgui::button>("Reset Navigation", .3f, [inputs, values]() {
        const auto defaults = values(editor_preferences{});
        for (std::size_t i = 0; i < inputs.size(); ++i)
            inputs[i]->set_buffer(format_number(defaults[i]));
    });
    content.add<bgui::button>("Apply", .3f,
        [context = m_context, inputs, &grid, &status, &modal]() {
            editor_preferences candidate;
            candidate.show_grid = grid.checked();
            const std::array fields{&candidate.movement_speed, &candidate.sprint_multiplier,
                &candidate.mouse_sensitivity, &candidate.zoom_sensitivity,
                &candidate.smoothing_speed};
            for (std::size_t i = 0; i < inputs.size(); ++i) {
                if (!parse_number(inputs[i]->get_buffer(), *fields[i])) {
                    status.set_buffer("Enter a finite number in every field.");
                    return;
                }
            }
            if (!candidate.valid()) {
                status.set_buffer("Use the range shown beside each setting.");
                return;
            }
            context->preferences = candidate;
            modal.set_enable(false);
            bgui::add_function([parent = modal.get_parent(), pointer = &modal]() {
                if (parent)
                    parent->remove(pointer);
            });
        });
    modal.set_confirmation_text("Cancel");
}

void editor_ui::set_exit_callback(std::function<void()> callback) {
    m_exit_callback = std::move(callback);
}

void editor_ui::confirm_exit() {
    if (!m_project_persistence.has_unsaved_changes(m_project_path)) {
        if (m_exit_callback)
            m_exit_callback();
        return;
    }
    if (!m_exit_modal)
        create_exit_modal();
    else
        m_exit_modal->set_enable(true);
}

void editor_ui::update() {
    for (auto& panel : m_panels)
        if (panel)
            panel->update();
}

}
