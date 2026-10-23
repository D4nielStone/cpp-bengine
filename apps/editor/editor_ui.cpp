#include "editor_ui.hpp"

#include <components/directional_light.hpp>

#include <elem/button.hpp>
#include <elem/icon.hpp>
#include <elem/inputbox.hpp>
#include <elem/menu_bar.hpp>
#include <elem/modal.hpp>
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
    std::string project_path
)
    : m_dock(dock),
      m_project_persistence(project_persistence),
      m_registry(std::move(registry)),
      m_project_path(std::move(project_path)) {
    create_project_modal(m_project_path, "");

    menu_bar.add_menu(
        "Files",
        0.3f,
        [this, ui_persistence_ptr = &ui_persistence](bgui::context_menu& menu) {
            menu.add_button("Save Project", [this]() {
                std::string error;
                const bool success = m_project_persistence.save(m_project_path, error);
                report_result("Saving project", success, error);
            });
            menu.add_button("Load Project", [this]() {
                if (!m_project_modal)
                    create_project_modal(m_project_path, "");

                std::error_code error;
                auto start_directory = std::filesystem::path(m_project_path);
                const bool is_directory =
                    std::filesystem::is_directory(start_directory, error);
                if (error) {
                    m_project_picker_status->set_buffer(
                        "Could not inspect current project path: " +
                        error.message()
                    );
                    if (m_project_modal)
                        m_project_modal->set_enable(true);
                    return;
                }
                if (!is_directory)
                    start_directory = start_directory.parent_path();
                if (start_directory.empty()) {
                    start_directory = std::filesystem::current_path(error);
                }
                if (error) {
                    m_project_picker_status->set_buffer(
                        "Could not locate a folder to browse: " +
                        error.message()
                    );
                } else {
                    refresh_project_picker(start_directory.string());
                }
                if (m_project_modal)
                    m_project_modal->set_enable(true);
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
        "Settings",
        0.3f,
        [this](bgui::context_menu& menu) {
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
    add_vector("Direction", m_light_direction_inputs);
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

void editor_ui::refresh_project_picker(const std::string& directory) {
    if (!m_project_directory_input || !m_project_directory_list)
        return;

    std::error_code error;
    auto path = std::filesystem::path(directory);
    if (path.empty())
        path = std::filesystem::current_path(error);
    if (error) {
        m_project_picker_status->set_buffer(
            "Could not resolve the current directory: " + error.message()
        );
        return;
    }

    path = std::filesystem::absolute(path, error).lexically_normal();
    if (error) {
        m_project_picker_status->set_buffer(
            "Could not resolve directory: " + error.message()
        );
        return;
    }

    if (!std::filesystem::is_directory(path, error) || error) {
        m_project_picker_status->set_buffer(
            error ? "Could not inspect directory: " + error.message()
                  : "The selected path is not a directory."
        );
        return;
    }

    m_project_directory_input->set_buffer(path.string());
    m_project_picker_status->set_buffer(
        "Choose a folder, or select a .bengine, .world, or .scene file."
    );

    for (auto& [layer, elements] : m_project_directory_list->get_elements()) {
        (void)layer;
        while (!elements.empty())
            m_project_directory_list->remove(elements.back().get());
    }

    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> project_files;
    std::filesystem::directory_iterator iterator(path, error);
    if (error) {
        m_project_picker_status->set_buffer(
            "Could not list directory: " + error.message()
        );
        return;
    }

    const std::filesystem::directory_iterator end;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            m_project_picker_status->set_buffer(
                "Could not finish listing directory: " + error.message()
            );
            return;
        }

        std::error_code entry_error;
        if (iterator->is_directory(entry_error) && !entry_error) {
            directories.push_back(iterator->path());
            continue;
        }
        if (entry_error || !iterator->is_regular_file(entry_error) || entry_error)
            continue;

        auto extension = iterator->path().extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            }
        );
        if (extension == ".bengine" || extension == ".world" ||
            extension == ".scene") {
            project_files.push_back(iterator->path());
        }
    }
    if (error) {
        m_project_picker_status->set_buffer(
            "Could not finish listing directory: " + error.message()
        );
        return;
    }

    const auto by_name = [](const auto& left, const auto& right) {
        return left.filename().string() < right.filename().string();
    };
    std::sort(directories.begin(), directories.end(), by_name);
    std::sort(project_files.begin(), project_files.end(), by_name);

    for (const auto& entry : directories) {
        auto& item = m_project_directory_list->add<bgui::button>(
            "[Folder] " + entry.filename().string(),
            0.28f,
            [this, entry]() { refresh_project_picker(entry.string()); }
        );
        item.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
    }
    for (const auto& entry : project_files) {
        auto& item = m_project_directory_list->add<bgui::button>(
            "[Project] " + entry.filename().string(),
            0.28f,
            [this, entry]() {
                if (m_project_directory_input)
                    m_project_directory_input->set_buffer(entry.string());
                if (m_project_picker_status)
                    m_project_picker_status->set_buffer(
                        "Selected project file: " + entry.filename().string()
                    );
            }
        );
        item.style.layout.require_mode(
            bgui::mode::match_parent,
            bgui::mode::wrap_content
        );
    }

    if (directories.empty() && project_files.empty())
        m_project_picker_status->set_buffer(
            "This folder has no subdirectories or supported project files."
        );
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
    m_project_modal->content().add<bgui::text>(
        "Choose a project folder or a project file.",
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
    path_actions.add<bgui::button>(
        "Parent folder",
        0.3f,
        [this]() {
            if (!m_project_directory_input)
                return;
            const auto parent = std::filesystem::path(
                m_project_directory_input->get_buffer()
            ).parent_path();
            if (!parent.empty())
                refresh_project_picker(parent.string());
        }
    );
    m_project_picker_status = &m_project_modal->content().add<bgui::text>(
        status,
        0.28f
    );
    m_project_directory_list = &m_project_modal->content().add<bgui::linear>(
        bgui::orientation::vertical
    );
    m_project_directory_list->style.layout.require_mode(
        bgui::mode::match_parent,
        bgui::mode::pixel
    );
    m_project_directory_list->style.layout.require_height(
        bgui::mode::pixel,
        220.f
    );
    m_project_directory_list->set_scrollable(true);
    m_project_modal->set_confirmation_text("Open");
    m_project_modal->set_on_confirm([this]() {
        const auto selected_path = m_project_directory_input
            ? m_project_directory_input->get_buffer()
            : std::string{};
        const bool loaded = load_project_selection();
        if (loaded) {
            m_project_modal = nullptr;
            m_project_directory_input = nullptr;
            m_project_directory_list = nullptr;
            m_project_picker_status = nullptr;
            return;
        }

        const auto status = m_project_picker_status
            ? m_project_picker_status->get_buffer()
            : std::string{"Could not load the selected project."};
        create_project_modal(selected_path, status);
        const auto directory = std::filesystem::path(selected_path).parent_path();
        if (!directory.empty())
            refresh_project_picker(directory.string());
        if (m_project_directory_input)
            m_project_directory_input->set_buffer(selected_path);
        if (m_project_picker_status)
            m_project_picker_status->set_buffer(status);
        m_project_modal->set_enable(true);
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

void editor_ui::set_exit_callback(std::function<void()> callback) {
    m_exit_callback = std::move(callback);
}

void editor_ui::confirm_exit() {
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