#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include "editor_ui.hpp"
#include "files_view.hpp"
#include "game_view.hpp"
#include "components_view.hpp"
#include "undo_redo_shortcuts_system.hpp"
#include <elem/button.hpp>
#include <elem/checkbox.hpp>
#include <elem/field.hpp>
#include <elem/icon.hpp>
#include <elem/inputbox.hpp>
#include <elem/window.hpp>
#include <components/camera.hpp>
#include <components/transform.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>

namespace {
class counting_panel final : public editor::ui::editor_panel {
public:
    void build(bgui::dock&) override { ++build_count; }
    void update() override { ++update_count; }

    int build_count{0};
    int update_count{0};
};

template<class T, class Predicate>
T* find(bgui::element& root, Predicate predicate) {
    if (auto* item = dynamic_cast<T*>(&root); item && predicate(*item))
        return item;
    if (auto* layout = root.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* result = find<T>(*child, predicate))
                    return result;
    return nullptr;
}

class EditorMenuTest : public testing::Test {
protected:
    bgui::scoped_interface ui_scope;
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    std::shared_ptr<editor::ui::editor_context> context =
        std::make_shared<editor::ui::editor_context>(registry);
    bgui::linear& root = bgui::get_layout().add<bgui::linear>();
    bgui::menu_bar& menu = root.add<bgui::menu_bar>(root);
    bgui::linear& side_bar = root.add<bgui::linear>(bgui::orientation::vertical);
    bgui::dock& dock = root.add<bgui::dock>();
    bgui::dock& model_dock = root.add<bgui::dock>();
    bgui::dock& management_dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::project_persistence_system persistence{registry, 0};
    editor::ui::editor_ui editor{
        dock,
        model_dock,
        management_dock,
        menu,
        side_bar,
        ui_persistence,
        persistence,
        registry,
        "project.bengine",
        "/tmp/bengine-build"
    };

    void SetUp() override {
        bgui::get_context().m_input_map.clear();
        bgui::get_context().m_size = {1200, 800};
        bgui::set_global_scale(1.f);
        editor.set_editor_context(context);
        root.add<editor::ui::undo_redo_shortcuts_system>(context);
    }
    void TearDown() override { bgui::set_global_scale(1.f); }
    void click(const std::string& label) {
        auto* button = find<bgui::button>(bgui::get_layout(), [&](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(button, nullptr) << label;
        button->on_pressed();
        button->on_released();
        bgui::on_update();
    }
};
}

TEST_F(EditorMenuTest, ViewZoomActionsChangeTheGlobalInterfaceScale) {
    click("Zoom In     Ctrl +");
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.1f);
    click("Zoom Out    Ctrl -");
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
    click("Zoom In     Ctrl +");
    click("Actual Size (100%)    Ctrl 0");
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
}

TEST_F(EditorMenuTest, WorkspaceModeSelectsItsOwnDock) {
    ASSERT_TRUE(side_bar.style.visual.background.normal.has_value());
    EXPECT_LT(side_bar.style.visual.background.normal->x, 0.1f);
    EXPECT_LT(side_bar.style.visual.background.normal->y, 0.1f);
    EXPECT_LT(side_bar.style.visual.background.normal->z, 0.1f);
    ASSERT_TRUE(side_bar.style.layout.padding.has_value());
    EXPECT_EQ(side_bar.style.layout.padding->x, 0);
    EXPECT_EQ(side_bar.style.layout.padding->z, 0);
    auto* file_button = find<bgui::button>(menu, [](auto& button) {
        return button.get_label().get_buffer() == "File";
    });
    ASSERT_NE(file_button, nullptr);
    bgui::on_update();
    EXPECT_GE(file_button->processed_x(), 14);
    ASSERT_TRUE(file_button->style.layout.margin.has_value());
    EXPECT_EQ(file_button->style.layout.margin->x, 14);
    EXPECT_EQ(file_button->style.layout.margin->z, 0);
    std::size_t workspace_button_count = 0;
    for (const auto& entry : side_bar.get_elements()) {
        for (const auto& child : entry.second) {
            auto* button = dynamic_cast<bgui::button*>(child.get());
            if (!button)
                continue;

            ++workspace_button_count;
            EXPECT_TRUE(button->get_label().get_buffer().empty());
            EXPECT_FALSE(button->get_label().is_enabled());
            EXPECT_NE(find<bgui::icon>(
                *button,
                [](const auto&) { return true; }
            ), nullptr);
            ASSERT_TRUE(button->style.layout.align.has_value());
            EXPECT_EQ(
                (*button->style.layout.align)[0],
                bgui::alignment::center
            );
            EXPECT_EQ(
                (*button->style.layout.align)[1],
                bgui::alignment::center
            );
            ASSERT_TRUE(button->style.layout.margin.has_value());
            EXPECT_EQ(button->style.layout.margin->x, 0);
            EXPECT_EQ(button->style.layout.margin->z, 0);
            ASSERT_TRUE(button->style.visual.background.normal.has_value());
            EXPECT_FLOAT_EQ(button->style.visual.background.normal->a, 0.f);
        }
    }
    EXPECT_EQ(workspace_button_count, 4u);

    EXPECT_TRUE(dock.is_enabled());
    EXPECT_FALSE(model_dock.is_enabled());
    EXPECT_FALSE(management_dock.is_enabled());

    editor.set_workspace_mode(editor::ui::editor_workspace_mode::model_editing);
    EXPECT_FALSE(dock.is_enabled());
    EXPECT_TRUE(model_dock.is_enabled());
    EXPECT_FALSE(management_dock.is_enabled());

    editor.set_workspace_mode(
        editor::ui::editor_workspace_mode::scene_management
    );
    EXPECT_FALSE(dock.is_enabled());
    EXPECT_FALSE(model_dock.is_enabled());
    EXPECT_TRUE(management_dock.is_enabled());

    editor.set_workspace_mode(editor::ui::editor_workspace_mode::editor);
    EXPECT_TRUE(dock.is_enabled());
    EXPECT_FALSE(model_dock.is_enabled());
    EXPECT_FALSE(management_dock.is_enabled());
}

TEST_F(EditorMenuTest, SavedProjectExitsWithoutOpeningSaveConfirmation) {
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system project(registry, camera.id);
    const auto path = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
        ("bengine-exit-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()) + ".bengine");
    std::string error;
    ASSERT_TRUE(project.save(path.string(), error)) << error;
    editor::ui::editor_ui ui(
        dock,
        model_dock,
        management_dock,
        menu,
        side_bar,
        ui_persistence,
        project,
        registry,
        path.string(),
        "/tmp/bengine-build"
    );
    bool exited = false;
    ui.set_exit_callback([&]() { exited = true; });
    ui.confirm_exit();
    EXPECT_TRUE(exited);
    exited = false;
    registry->get<commons::transform>(camera.id)->set_position({3.f, 0.f, 0.f});
    ui.confirm_exit();
    EXPECT_FALSE(exited);
    auto* confirm = find<bgui::button>(bgui::get_layout(), [](auto& item) {
        return item.get_label().get_buffer() == "Save and Exit" && item.is_enabled();
    });
    ASSERT_NE(confirm, nullptr);
    click("Save and Exit");
    EXPECT_TRUE(exited);
    EXPECT_FALSE(project.has_unsaved_changes(path.string()));
    std::filesystem::remove(path);
    auto scene = path;
    scene.replace_extension(".scene");
    std::filesystem::remove(scene);
}

TEST(EditorCommandsTest, DispatchesHistoryAndColliderCommands) {
    auto context = std::make_shared<editor::ui::editor_context>(
        std::make_shared<commons::ecs>()
    );
    editor::ui::editor_commands commands(context);
    int undo_count = 0;
    int redo_count = 0;
    context->record_action(
        [&]() { ++undo_count; },
        [&]() { ++redo_count; }
    );

    EXPECT_TRUE(commands.execute(editor::ui::editor_command::undo));
    EXPECT_TRUE(commands.execute(editor::ui::editor_command::redo));
    EXPECT_EQ(undo_count, 1);
    EXPECT_EQ(redo_count, 1);
    EXPECT_FALSE(context->show_colliders);
    EXPECT_TRUE(commands.execute(editor::ui::editor_command::toggle_colliders));
    EXPECT_TRUE(context->show_colliders);
}

TEST(PanelManagerTest, OwnsPanelsAndForwardsUpdates) {
    bgui::scoped_interface ui_scope;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::panel_manager panels(dock);

    auto& panel = panels.add<counting_panel>();
    EXPECT_EQ(panel.build_count, 1);
    EXPECT_EQ(panel.update_count, 0);

    panels.update();
    EXPECT_EQ(panel.update_count, 1);
}

TEST(GameViewTest, PreviewsRuntimeCameraWhileTheRuntimeIsRunning) {
    bgui::scoped_interface ui_scope;
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(1);
    registry->add<commons::camera>(editor_camera);
    auto game_camera = registry->create(2);
    registry->add<commons::camera>(game_camera);
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->editor_camera_entity = editor_camera.id;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::game_view panel(context);
    panel.build(dock);

    EXPECT_TRUE(panel.needs_camera_render(game_camera.id));
    EXPECT_FALSE(panel.needs_camera_render(editor_camera.id));
    auto* window = find<bgui::window>(dock, [](auto& value) {
        return value.get_title().get_buffer() == "Game";
    });
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->is_enabled());

    context->is_playing = true;
    panel.update();
    EXPECT_TRUE(window->is_enabled());
    EXPECT_TRUE(panel.needs_camera_render(game_camera.id));

    context->is_playing = false;
    panel.update();
    EXPECT_TRUE(window->is_enabled());
    EXPECT_TRUE(panel.needs_camera_render(game_camera.id));
    window->set_enable(false);
    panel.update();
    EXPECT_FALSE(window->is_enabled());
    EXPECT_FALSE(panel.needs_camera_render(game_camera.id));
}

TEST(GameViewTest, ClosingDockedGamePanelStopsUpdates) {
    bgui::scoped_interface ui_scope;
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::game_view panel(context);
    panel.build(dock);
    auto* window = find<bgui::window>(dock, [](auto& value) {
        return value.get_title().get_buffer() == "Game";
    });
    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(panel.needs_camera_render(camera.id));

    bgui::on_update();
    ASSERT_TRUE(dock.remove_window(window));
    panel.update();
    bgui::on_update();
    EXPECT_FALSE(panel.needs_camera_render(camera.id));
}

TEST_F(EditorMenuTest, UnsavedProjectRequiresConfirmationAndCancelKeepsEditorOpen) {
    bool exited = false;
    editor.set_exit_callback([&]() { exited = true; });
    editor.confirm_exit();
    EXPECT_FALSE(exited);
    click("Cancel");
    EXPECT_FALSE(exited);
    editor.confirm_exit();
    click("Discard and Exit");
    EXPECT_TRUE(exited);
}

TEST_F(EditorMenuTest, LayoutPresetsDockPanelsInTheirExpectedRegions) {
    dock.add_window("Scene", bgui::dock_area::left);
    dock.add_window("Hierarchy", bgui::dock_area::center);
    dock.add_window(editor::ui::tr("Arquivos"), bgui::dock_area::right);
    dock.add_window("Debug", bgui::dock_area::right);
    click("Layout: Wide Scene");
    auto configuration = dock.get_configuration();
    EXPECT_FLOAT_EQ(configuration.left_ratio, .16f);
    EXPECT_FLOAT_EQ(configuration.right_ratio, .18f);
    for (const auto& window : configuration.windows) {
        EXPECT_FALSE(window.floating);
        if (window.title == "Scene") EXPECT_EQ(window.area, bgui::dock_area::center);
        if (window.title == "Hierarchy") EXPECT_EQ(window.area, bgui::dock_area::left);
        if (window.title == editor::ui::tr("Arquivos")) EXPECT_EQ(window.area, bgui::dock_area::bottom);
    }
    ASSERT_EQ(configuration.tab_groups.size(), 1u);
    EXPECT_EQ(configuration.tab_groups.front().windows, (std::vector<std::string>{editor::ui::tr("Arquivos"), "Debug"}));
    click("Layout: Standard");
    EXPECT_FLOAT_EQ(dock.get_configuration().left_ratio, .24f);
}

TEST_F(EditorMenuTest, EditorCameraActionOpensSettingsModalAndAppliesProjection) {
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto camera = registry->get<commons::camera>(entity.id);
    context->editor_camera_entity = entity.id;
    auto& components = editor.add_panel<editor::ui::components_view>(context);
    auto* inspector = find<bgui::window>(dock, [](auto& window) {
        return window.get_title().get_buffer() == "Inspector";
    });
    ASSERT_NE(inspector, nullptr);
    auto& other = dock.add_window("Other", bgui::dock_area::right);
    dock.merge_window_as_tab(*inspector, other);

    click("Editor Camera...");

    EXPECT_EQ(context->selected_entity, entity.id);
    const auto configuration = dock.get_configuration();
    ASSERT_EQ(configuration.tab_groups.size(), 1u);
    EXPECT_EQ(configuration.tab_groups.front().active, "Inspector");
    components.update();
    EXPECT_NE(find<bgui::field>(dock, [](auto& field) {
        return field.get_label().get_buffer() == "Field of View";
    }), nullptr);

    auto* camera_modal = find<bgui::modal>(bgui::get_layout(), [](auto& modal) {
        return find<bgui::text>(modal, [](auto& text) {
            return text.get_buffer() == "Editor Camera Settings";
        }) != nullptr;
    });
    ASSERT_NE(camera_modal, nullptr);
    auto* fov = find<bgui::field>(*camera_modal, [](auto& field) {
        return field.get_label().get_buffer() == "Field of View";
    });
    ASSERT_NE(fov, nullptr);
    fov->get_inputbox().set_buffer("200");
    click("Apply");
    EXPECT_FLOAT_EQ(camera->config.fov, 75.f);
    EXPECT_NE(find<bgui::modal>(bgui::get_layout(), [](auto& modal) {
        return find<bgui::text>(modal, [](auto& text) {
            return text.get_buffer() == "Editor Camera Settings";
        }) != nullptr;
    }), nullptr);

    fov->get_inputbox().set_buffer("90");
    const auto set_camera_field = [camera_modal](const std::string& label, const std::string& value) {
        auto* field = find<bgui::field>(*camera_modal, [&](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(field, nullptr) << label;
        field->get_inputbox().set_buffer(value);
    };
    set_camera_field("Near Clip", "0.2");
    set_camera_field("Far Clip", "500");
    set_camera_field("Orthographic Scale", "10");
    auto* orthographic = find<bgui::checkbox>(*camera_modal, [](auto& item) {
        return item.get_label().get_buffer() == "Orthographic";
    });
    ASSERT_NE(orthographic, nullptr);
    orthographic->on_clicked();
    click("Apply");
    EXPECT_FLOAT_EQ(camera->config.fov, 90.f);
    EXPECT_FLOAT_EQ(camera->config.corte_curto, 0.2f);
    EXPECT_FLOAT_EQ(camera->config.corte_longo, 500.f);
    EXPECT_FLOAT_EQ(camera->config.scale, 10.f);
    EXPECT_TRUE(camera->config.flag_orth);
    EXPECT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(camera->config.fov, 75.f);
    EXPECT_FLOAT_EQ(camera->config.corte_curto, 0.1f);
    EXPECT_FLOAT_EQ(camera->config.corte_longo, 300.f);
    EXPECT_FLOAT_EQ(camera->config.scale, 5.f);
    EXPECT_FALSE(camera->config.flag_orth);
}

TEST_F(EditorMenuTest, PreferencesApplyGridAndNavigationValues) {
    click("Editor Preferences...");
    auto* grid = find<bgui::checkbox>(bgui::get_layout(), [](auto&) { return true; });
    ASSERT_NE(grid, nullptr);
    grid->on_clicked();
    auto* speed = find<bgui::inputbox>(bgui::get_layout(), [](auto& item) {
        return item.get_buffer() == "5";
    });
    ASSERT_NE(speed, nullptr);
    speed->set_buffer("18");
    for (const auto& [previous, replacement] : std::array{
            std::pair{"3", "4"}, std::pair{"0.15", "0.3"},
            std::pair{"0.8", "1.5"}, std::pair{"12", "20"}}) {
        auto* input = find<bgui::inputbox>(bgui::get_layout(), [&](auto& item) {
            return item.get_buffer() == previous;
        });
        ASSERT_NE(input, nullptr) << previous;
        input->set_buffer(replacement);
    }
    click("Apply");
    EXPECT_FALSE(context->preferences.show_grid);
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 18.f);
    EXPECT_FLOAT_EQ(context->preferences.sprint_multiplier, 4.f);
    EXPECT_FLOAT_EQ(context->preferences.mouse_sensitivity, .3f);
    EXPECT_FLOAT_EQ(context->preferences.zoom_sensitivity, 1.5f);
    EXPECT_FLOAT_EQ(context->preferences.smoothing_speed, 20.f);
}

TEST_F(EditorMenuTest, InvalidPreferencesKeepPreviousValuesAndCanBeCorrected) {
    click("Editor Preferences...");
    auto* speed = find<bgui::inputbox>(bgui::get_layout(), [](auto& item) {
        return item.get_buffer() == "5";
    });
    ASSERT_NE(speed, nullptr);
    speed->set_buffer("nan");
    click("Apply");
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 5.f);
    speed->set_buffer("101");
    click("Apply");
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 5.f);
    speed->set_buffer("10");
    click("Apply");
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 10.f);
}

TEST_F(EditorMenuTest, CancelDoesNotApplyAndResetNavigationRestoresDefaults) {
    context->preferences.movement_speed = 25.f;
    click("Editor Preferences...");
    click("Reset Navigation");
    click("Cancel");
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 25.f);
    click("Editor Preferences...");
    click("Reset Navigation");
    click("Apply");
    EXPECT_FLOAT_EQ(context->preferences.movement_speed, 5.f);
    EXPECT_TRUE(context->preferences.valid());
}

TEST(EditorPreferencesTest, EveryNavigationValueRejectsNonFiniteAndOutOfRangeValues) {
    editor::ui::editor_preferences preferences;
    EXPECT_TRUE(preferences.valid());
    const std::array fields{&preferences.movement_speed, &preferences.sprint_multiplier,
        &preferences.mouse_sensitivity, &preferences.zoom_sensitivity,
        &preferences.smoothing_speed};
    for (auto* field : fields) {
        const float previous = *field;
        for (const float invalid : {-1.f, 0.f, 101.f, INFINITY, NAN}) {
            *field = invalid;
            EXPECT_FALSE(preferences.valid());
        }
        *field = previous;
    }
}

TEST_F(EditorMenuTest, ResetUiRestoresStandardLayoutAndScaleWithoutClosingDocuments) {
    dock.add_window("Scene", bgui::dock_area::left);
    dock.add_window("open.lua", bgui::dock_area::right);
    click("Layout: Wide Scene");
    click("Zoom In     Ctrl +");
    click("Reset UI");
    const auto configuration = dock.get_configuration();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
    EXPECT_FLOAT_EQ(configuration.left_ratio, .24f);
    EXPECT_FLOAT_EQ(configuration.right_ratio, .24f);
    ASSERT_EQ(configuration.windows.size(), 2u);
    for (const auto& window : configuration.windows) {
        EXPECT_FALSE(window.floating);
        if (window.title == "Scene") EXPECT_EQ(window.area, bgui::dock_area::center);
    }
}


TEST_F(EditorMenuTest, F3AndViewMenuShareColliderVisibilityWithoutRepeatToggling) {
    EXPECT_FALSE(context->show_colliders);
    bgui::get_context().m_input_map[bgui::input_key::f3] = bgui::input_action::press;
    bgui::on_update(); EXPECT_TRUE(context->show_colliders);
    bgui::get_context().m_input_map[bgui::input_key::f3] = bgui::input_action::repeat;
    bgui::on_update(); EXPECT_TRUE(context->show_colliders);
    bgui::get_context().m_input_map[bgui::input_key::f3] = bgui::input_action::release;
    bgui::on_update();
    click("Show/Hide Colliders    F3");
    EXPECT_FALSE(context->show_colliders);
    bgui::get_context().m_input_map[bgui::input_key::f3] = bgui::input_action::press;
    bgui::on_update(); EXPECT_TRUE(context->show_colliders);
}

TEST(EditorExportTest, BuildOutputDirectoryDoesNotReplaceEngineSourceDirectory) {
    bgui::scoped_interface ui_scope;
    const auto temporary = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
        ("bengine menu export " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary);
    const auto output = temporary / "output";
    const auto project = temporary / "project.bengine";
    const char* cache = std::getenv(
#ifdef _WIN32
        "LOCALAPPDATA"
#else
        "XDG_CACHE_HOME"
#endif
    );
    const auto cache_root = cache && *cache ? std::filesystem::path(cache) : std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    const auto key = std::string(BENGINE_REPOSITORY_ROOT) + "|" + project.string() + "|" + output.string() + "|linux";
    const auto workspace = cache_root / "cpp-bengine/game-builds" / std::to_string(std::hash<std::string>{}(key));
    std::filesystem::create_directories(workspace / "editor-build-linux.progress");
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create(); registry->add<commons::camera>(camera);
    auto& root = bgui::get_layout().add<bgui::linear>();
    auto& menu = root.add<bgui::menu_bar>(root);
    auto& side_bar = root.add<bgui::linear>(bgui::orientation::vertical);
    auto& dock = root.add<bgui::dock>();
    auto& model_dock = root.add<bgui::dock>();
    auto& management_dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui;
    editor::ui::project_persistence_system persistence(registry, camera.id);
    editor::ui::editor_ui editor(
        dock,
        model_dock,
        management_dock,
        menu,
        side_bar,
        ui,
        persistence,
        registry,
        project.string(),
        temporary / "build"
    );
    const auto click = [&](const std::string& label) {
        auto* button = find<bgui::button>(bgui::get_layout(), [&](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(button, nullptr) << label;
        button->on_pressed(); button->on_released(); bgui::on_update();
    };
    click("Build"); click("Export Game");
    auto* input = find<bgui::inputbox>(bgui::get_layout(), [&](auto& item) {
        return item.get_buffer() == (temporary / "build/exports").lexically_normal().string();
    });
    ASSERT_NE(input, nullptr);
    input->set_buffer(output.string()); input->m_enter_func(output.string());
    click("Build Linux");
    auto* status = find<bgui::text>(bgui::get_layout(), [](auto& item) {
        return item.get_buffer().find("Could not create build progress file") != std::string::npos;
    });
    EXPECT_NE(status, nullptr);
    EXPECT_TRUE(std::filesystem::is_regular_file(project)); // Current scene was saved.
    EXPECT_FALSE(std::filesystem::exists(output)); // No logs/build files in the output.
    std::filesystem::remove_all(workspace);
    std::filesystem::remove_all(temporary);
}

TEST_F(EditorMenuTest, ExportModalOffersOptionalSingleExecutableAndRuntimeTemplate) {
    click("Build");click("Export Game");
    auto* compact=find<bgui::checkbox>(bgui::get_layout(),[](auto& item){
        return item.get_label().get_buffer().find("Single executable")!=std::string::npos;
    });
    ASSERT_NE(compact,nullptr);EXPECT_FALSE(compact->checked());
    compact->on_clicked();bgui::on_update();
    EXPECT_TRUE(compact->checked());
    auto* template_input=find<bgui::inputbox>(bgui::get_layout(),[](auto& item){return item.get_buffer().empty();});
    ASSERT_NE(template_input,nullptr);
}

TEST(EditorProjectManager, ContextMenuSeparatesRemovingFromListAndDeletingFolder) {
    bgui::scoped_interface ui_scope;
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / ("bengine-project-menu-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder / "Example");
    struct cleanup {
        fs::path previous, folder;
        ~cleanup() { fs::current_path(previous); std::error_code error; fs::remove_all(folder, error); }
    } cleanup{fs::current_path(), folder};
    fs::current_path(folder);
    const auto project = folder / "Example/project.bengine";
    std::ofstream(project) << R"({"format":"bubble-engine-project","version":1})";
    std::ofstream("editor-recent-projects.cfg") << project.string() << '\n';
    auto registry = std::make_shared<commons::ecs>();
    auto& root = bgui::get_layout();
    auto& menu = root.add<bgui::menu_bar>(root);
    auto& side_bar = root.add<bgui::linear>(bgui::orientation::vertical);
    auto& dock = root.add<bgui::dock>();
    auto& model_dock = root.add<bgui::dock>();
    auto& management_dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::project_persistence_system persistence(registry, 0);
    editor::ui::editor_ui editor(
        dock,
        model_dock,
        management_dock,
        menu,
        side_bar,
        ui_persistence,
        persistence,
        registry,
        "other.bengine",
        folder
    );
    auto* manager = find<bgui::modal>(root, [](auto& value) {
        return find<bgui::text>(value, [](auto& text) { return text.get_buffer() == "Project Manager"; });
    });
    ASSERT_NE(manager, nullptr);
    manager->set_enable(true);
    auto* row = find<bgui::button>(*manager, [&](auto& value) {
        return value.get_label().get_buffer().find(project.string()) != std::string::npos;
    });
    ASSERT_NE(row, nullptr);
    bgui::get_context().m_size = {1200, 800};
    bgui::on_update();
    bgui::get_context().m_mouse_position = {row->processed_x() + 6, row->processed_y() + 6};
    bgui::get_context().m_input_map[bgui::input_key::mouse_right] = bgui::input_action::press;
    bgui::on_update(); bgui::on_update();
    auto* popup = find<bgui::linear>(root, [](auto& value) { return value.has_class("editor-context-actions") && value.is_enabled(); });
    ASSERT_NE(popup, nullptr);
    auto* remove = find<bgui::button>(*popup, [](auto& value) { return value.get_label().get_buffer() == "Remover da lista"; });
    auto* erase = find<bgui::button>(*popup, [](auto& value) { return value.get_label().get_buffer() == "Apagar pasta do disco..."; });
    ASSERT_NE(remove, nullptr); ASSERT_NE(erase, nullptr);
    remove->on_released(); bgui::get_context().m_input_map.clear(); bgui::on_update();
    EXPECT_TRUE(fs::exists(project));
    std::ifstream input("editor-recent-projects.cfg");
    EXPECT_TRUE(std::string(std::istreambuf_iterator<char>(input), {}).empty());
    EXPECT_FALSE(row->is_enabled());
}

#include "editor_workspace.hpp"

TEST(EditorWorkflow, RestoresSelectionModesCameraAndInspector) {
    bgui::scoped_interface ui_scope;
    const auto root = std::filesystem::temp_directory_path() /
        ("bubble-workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::camera>(entity);
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->editor_camera_entity = entity.id;
    context->selected_entity = entity.id;
    context->workspace_mode = 3;
    context->model_selection = 3;
    context->model_view_mode = 2;
    context->orbital_view = true;
    context->model_orbital_view = true;
    context->inspector_sections["Camera"] = false;
    auto camera = registry->get<commons::camera>(entity.id);
    camera->config.atmosphere_zenith = {.2f, .3f, .4f};
    editor::ui::ui_persistence_system persistence;
    editor::ui::editor_workspace workspace(persistence, context);
    workspace.set_project_file(root / "test.bengine");
    editor::ui::files_view files(root / "test.bengine");
    std::string error;
    ASSERT_TRUE(workspace.save_workspace(files, error)) << error;
    context->selected_entity.reset(); context->workspace_mode = 0;
    context->model_selection = 0; context->model_view_mode = 0; context->orbital_view = false; context->model_orbital_view = false;
    context->inspector_sections.clear();
    camera->config.atmosphere_zenith = {};
    ASSERT_TRUE(workspace.load_workspace(files, error)) << error;
    EXPECT_EQ(context->selected_entity, entity.id);
    EXPECT_EQ(context->workspace_mode, 3);
    EXPECT_EQ(context->model_selection, 3);
    EXPECT_EQ(context->model_view_mode, 2);
    EXPECT_TRUE(context->orbital_view);
    EXPECT_TRUE(context->model_orbital_view);
    EXPECT_FALSE(context->inspector_sections.at("Camera"));
    EXPECT_EQ(camera->config.atmosphere_zenith, (bgui::vec3{.2f,.3f,.4f}));
    std::ofstream(root / ".editor-workflow.json") << "{invalid";
    EXPECT_FALSE(workspace.load_workspace(files, error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove_all(root);
}

#include "model_editor_view.hpp"

TEST(ModelEditorLayout, KeepsToolsInSidebarAndHeadersOutsidePreview) {
    bgui::scoped_interface ui_scope;
    bgui::get_context().m_size = {1000, 700};
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    auto& dock = bgui::get_layout().add<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    editor::ui::model_editor_view panel(context);
    panel.build(dock);
    bgui::on_update();
    auto* distance = find<bgui::inputbox>(dock, [](auto& input) { return input.get_buffer() == "0.25"; });
    ASSERT_NE(distance, nullptr);
    EXPECT_LE(distance->processed_height(), 30 * bgui::get_global_scale());
    auto* extrude = find<bgui::button>(dock, [](auto& button) { return button.get_label().get_buffer() == "Extrudar face"; });
    ASSERT_NE(extrude, nullptr);
    auto* image = find<bgui::image>(dock, [](auto& image) { return image.has_class("model-editor-preview"); });
    ASSERT_NE(image, nullptr);
    EXPECT_GE(distance->processed_x(), image->processed_x() + image->processed_width());
    EXPECT_GE(extrude->processed_x(), image->processed_x() + image->processed_width());
    for (const auto* label : {"Objeto", "Faces", "Vértices", "Arestas", "Sólido", "Material", "Render"}) {
        auto* button = find<bgui::button>(dock, [label](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(button, nullptr) << label;
        EXPECT_LE(button->processed_y() + button->processed_height(), image->processed_y());
        EXPECT_GE(button->processed_x(), image->processed_x());
        EXPECT_LE(button->processed_x() + button->processed_width(), image->processed_x() + image->processed_width());
    }
    EXPECT_GT(image->processed_height(), 200);
}

TEST(FilesContextMenu, OpensOnLeftClickAndRetiresWithSurface) {
    bgui::scoped_interface ui_scope;
    auto& io = bgui::get_context();
    io.m_size = {800, 600};
    io.m_input_map.clear();
    auto& surface = bgui::get_layout().add<editor::ui::context_action_surface>(bgui::orientation::vertical);
    surface.style.layout.require_size(400, 300);
    surface.receives_input(true);
    surface.context_options = [] { return editor::ui::context_actions{{"New file", [] {}}}; };
    bgui::on_update();
    io.m_mouse_position = {surface.processed_x() + 20, surface.processed_y() + 20};
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    bgui::on_update();
    bgui::on_update();
    auto* popup = find<editor::ui::action_popup>(bgui::get_layout(), [](auto&) { return true; });
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(popup->is_enabled());
    bgui::get_layout().remove(&surface);
    bgui::on_update();
    bgui::on_update();
    EXPECT_EQ(find<editor::ui::action_popup>(bgui::get_layout(), [](auto&) { return true; }), nullptr);
}

TEST(ModelEditorSelection, UiFramesFollowLiveGeometryAndClearRemovedComponents) {
    bgui::scoped_interface ui_scope;
    bgui::get_context().m_size = {1000, 700};
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->project_root = std::filesystem::temp_directory_path() /
        ("model-panel-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto first = registry->create();
    auto ancestor = registry->create();
    auto parent = registry->create();
    auto ancestor_transform = registry->get<commons::transform>(ancestor.id);
    auto parent_transform = registry->get<commons::transform>(parent.id);
    ancestor_transform->set_position({7.f, 4.f, -3.f});
    parent_transform->set_rotation({30.f, 0.f, 0.f});
    parent_transform->set_scale({2.f, 3.f, 1.f});
    ASSERT_TRUE(parent_transform->set_parent(ancestor.id, false));
    auto model_transform = registry->get<commons::transform>(first.id);
    model_transform->set_position({1.f, 2.f, 0.f});
    ASSERT_TRUE(model_transform->set_parent(parent.id, false));
    auto second = registry->create();
    const std::vector<commons::vertex> vertices{
        {{-1.f,-1.f,0.f}, {}, {}}, {{1.f,-1.f,0.f}, {}, {}}, {{0.f,1.f,0.f}, {}, {}}
    };
    auto make_model = [&vertices](int count) {
        auto model = std::make_unique<commons::model>();
        for (int i = 0; i < count; ++i)
            model->meshes.emplace_back(vertices, std::vector<unsigned int>{0,1,2}, commons::material{});
        return model;
    };
    std::filesystem::create_directories(context->project_root);
    const auto native = context->project_root / "triangle.bmodel";
    commons::editable_model_data geometry;
    geometry.meshes.push_back({vertices, {{0, 1, 2}}});
    std::string save_error;
    ASSERT_TRUE(commons::save_editable_model(native, geometry, save_error)) << save_error;
    registry->add<commons::renderer>(first, new commons::model(native.string()));
    registry->add<commons::renderer>(second, make_model(2).release());
    context->entity_names[first.id] = "Triângulo em cena";
    context->entity_names[second.id] = "Outro objeto";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::model_editor_view panel(context);
    panel.build(dock);
    context->selected_entity = first.id;
    bgui::on_update();
    bgui::on_update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Triângulo em cena"; }), nullptr);
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Malha 1/1"; }), nullptr);
    auto* image = find<bgui::image>(dock, [](auto& image) { return image.has_class("model-editor-preview"); });
    ASSERT_NE(image, nullptr);
    const auto first_preview = image->get_texture().m_buffer;
    EXPECT_FALSE(first_preview.empty());
    const auto before_parent_move = panel.preview_camera().position;
    ancestor_transform->move({3.f, 0.f, 0.f});
    bgui::on_update();
    EXPECT_NEAR(panel.preview_camera().position.x - before_parent_move.x, 3.f, .001f);
    // Drag the existing gizmo's projected X axis, then save the changed mesh.
    const auto matrix = panel.preview_camera().projMatriz * panel.preview_camera().viewMatrix *
        model_transform->get_world_model_matrix();
    const auto screen = [&](bgui::vec3 p) {
        const auto clip = matrix*glm::vec4(p.x,p.y,p.z,1.f);
        return bgui::vec2{image->processed_x()+(clip.x/clip.w*.5f+.5f)*image->processed_width(),
            image->processed_y()+(.5f-clip.y/clip.w*.5f)*image->processed_height()};
    };
    const auto pivot = screen({0.f,-1.f/3.f,0.f});
    const auto unit = screen({1.f,-1.f/3.f,0.f}) - pivot;
    const auto direction = unit / std::sqrt(unit.x * unit.x + unit.y * unit.y);
    auto& io = bgui::get_context();
    io.m_input_map.clear();
    io.m_mouse_position = {int(pivot.x+direction.x*50), int(pivot.y+direction.y*50)};
    bgui::on_update();
    io.m_input_map[bgui::input_key::g] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    bgui::on_update();
    io.m_mouse_position = {int(pivot.x+direction.x*70), int(pivot.y+direction.y*70)};
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    auto* save = find<bgui::button>(dock, [](auto& button) {
        return button.get_label().get_buffer() == "Salvar .bmodel";
    });
    ASSERT_NE(save, nullptr);
    editor::ui::undo_redo_shortcuts_system shortcuts(
        std::make_shared<editor::ui::editor_commands>(context),
        [&](bool redo) { return panel.handle_history_shortcut(redo); },
        [&](editor::ui::model_shortcut action) {
            switch (action) {
                case editor::ui::model_shortcut::copy: panel.copy_selection(); break;
                case editor::ui::model_shortcut::paste: panel.paste_selection(); break;
                case editor::ui::model_shortcut::save: panel.save(); break;
                case editor::ui::model_shortcut::extract: panel.request_extract(); break;
            }
            return true;
        });
    const auto shortcut = [&](bgui::input_key key) {
        io.m_input_map[bgui::input_key::right_control] = bgui::input_action::press;
        io.m_input_map[key] = bgui::input_action::press;
        shortcuts.on_update();
        shortcuts.on_update();
        io.m_input_map.clear();
        shortcuts.on_update();
    };
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::z] = bgui::input_action::press;
    shortcuts.on_update();
    io.m_input_map.clear();
    shortcuts.on_update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Malha 1/1"; }), nullptr);
    save->on_released();
    bgui::on_update();
    commons::editable_model_data reverted;
    ASSERT_TRUE(commons::load_editable_model(native, reverted, save_error)) << save_error;
    ASSERT_EQ(reverted.meshes[0].vertices.size(), vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i)
        EXPECT_EQ(reverted.meshes[0].vertices[i].position, vertices[i].position);
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::left_shift] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::z] = bgui::input_action::press;
    shortcuts.on_update();
    io.m_input_map.clear();
    save->on_released();
    bgui::on_update();
    bgui::on_update();
    const auto saved_path = registry->get<commons::renderer>(first.id)->m_modelo->get_source_path();
    EXPECT_EQ(std::filesystem::path(saved_path), native);
    commons::editable_model_data saved;
    std::string error;
    ASSERT_TRUE(commons::load_editable_model(saved_path, saved, error)) << error;
    ASSERT_EQ(saved.meshes.size(), 1u);
    EXPECT_GT(saved.meshes[0].vertices[2].position.x, vertices[2].position.x + .01f);
    EXPECT_NEAR(saved.meshes[0].vertices[2].position.y, vertices[2].position.y, .001f);
    EXPECT_EQ(model_transform->get_position(), (bgui::vec3{1.f, 2.f, 0.f}));

    context->model_selection = static_cast<int>(editor::ui::model_selection_mode::edges);
    auto low = screen(saved.meshes[0].vertices.front().position);
    auto high = low;
    for (const auto& vertex : saved.meshes[0].vertices) {
        const auto point = screen(vertex.position);
        low = {std::min(low.x, point.x), std::min(low.y, point.y)};
        high = {std::max(high.x, point.x), std::max(high.y, point.y)};
    }
    const auto over_model = [] {
        const auto* target = bgui::get_mouse_target();
        return target && target->has_class("model-editor-viewport-input");
    };
    io.m_mouse_position = {int(low.x) - 8, int(low.y) - 8};
    bgui::on_update();
    ASSERT_TRUE(over_model());
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    bgui::on_update();
    io.m_mouse_position = {int(high.x) + 8, int(high.y) + 8};
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    ASSERT_NE(find<bgui::text>(dock, [](auto& text) {
        return text.get_buffer() == "Elementos selecionados: 3";
    }), nullptr);
    ASSERT_TRUE(over_model());
    shortcut(bgui::input_key::c);
    shortcut(bgui::input_key::v);
    shortcut(bgui::input_key::e);
    bgui::on_update();
    auto* export_modal = find<bgui::modal>(bgui::get_layout(), [](auto&) { return true; });
    ASSERT_NE(export_modal, nullptr);
    auto* confirm_export = find<bgui::button>(*export_modal, [](auto& button) {
        return button.has_class("modal-confirm");
    });
    ASSERT_NE(confirm_export, nullptr);
    confirm_export->on_released();
    bgui::on_update();
    commons::editable_model_data exported;
    ASSERT_TRUE(commons::load_editable_model(
        context->project_root / "assets/models/triangle_parts/triangle-malha-1.bmodel", exported, error)) << error;
    ASSERT_EQ(exported.meshes.size(), 1u);
    EXPECT_EQ(exported.meshes.front().faces.size(), 2u);
    ASSERT_TRUE(commons::load_editable_model(native, saved, error)) << error;
    EXPECT_EQ(saved.meshes.front().faces.size(), 1u);
    shortcut(bgui::input_key::s);
    ASSERT_TRUE(commons::load_editable_model(native, saved, error)) << error;
    EXPECT_EQ(saved.meshes.front().faces.size(), 2u);
    EXPECT_EQ(saved.meshes.front().vertices.size(), 6u);
    ASSERT_TRUE(panel.handle_history_shortcut(false));
    shortcut(bgui::input_key::s);
    ASSERT_TRUE(commons::load_editable_model(native, saved, error)) << error;
    EXPECT_EQ(saved.meshes.front().faces.size(), 1u);

    // Reselect the original triangle for the deletion check.
    io.m_mouse_position = {int(low.x) - 8, int(low.y) - 8};
    bgui::on_update();
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    bgui::on_update();
    io.m_mouse_position = {int(high.x) + 8, int(high.y) + 8};
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    io.m_input_map[bgui::input_key::delete_key] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    save->on_released();
    bgui::on_update();
    ASSERT_TRUE(commons::load_editable_model(native, saved, error)) << error;
    ASSERT_EQ(saved.meshes.size(), 1u);
    EXPECT_TRUE(saved.meshes[0].faces.empty());
    ASSERT_TRUE(panel.handle_history_shortcut(false));
    save->on_released();
    bgui::on_update();
    ASSERT_TRUE(commons::load_editable_model(native, saved, error)) << error;
    EXPECT_EQ(saved.meshes[0].faces.size(), 1u);
    context->selected_entity = second.id;
    bgui::on_update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Outro objeto"; }), nullptr);
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Malha 1/2"; }), nullptr);
    std::vector<bgui::tree*> groups;
    std::function<void(bgui::element&)> collect = [&](bgui::element& element) {
        if (auto* tree=dynamic_cast<bgui::tree*>(&element); tree && tree->has_class("model-mesh-section")) groups.push_back(tree);
        if (auto* layout=element.as_layout()) for (auto& [layer,children] : layout->get_elements()) for (auto& child : children) collect(*child);
    };
    collect(dock);
    ASSERT_EQ(groups.size(),2u);
    EXPECT_FALSE(groups[0]->is_expanded());
    groups[0]->set_expanded(true);
    auto* metallic=find<bgui::field>(*groups[0],[](auto& field) { return field.get_label().get_buffer()=="Metallic"; });
    ASSERT_NE(metallic,nullptr);
    metallic->get_inputbox().m_enter_func("0.8");
    auto renderer=registry->get<commons::renderer>(second.id);
    EXPECT_FLOAT_EQ(commons::pbr_value(renderer->m_modelo->meshes[0].getMaterial(),commons::pbr_factors[0]),.8f);
    EXPECT_FLOAT_EQ(commons::pbr_value(renderer->m_modelo->meshes[1].getMaterial(),commons::pbr_factors[0]),0.f);
    ASSERT_TRUE(context->undo());
    EXPECT_FLOAT_EQ(commons::pbr_value(renderer->m_modelo->meshes[0].getMaterial(),commons::pbr_factors[0]),0.f);
    const unsigned char bytes[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,11,73,68,65,84,120,156,99,248,15,4,0,9,251,3,253,251,94,107,43,0,0,0,0,73,69,78,68,174,66,96,130};
    const auto texture_path=context->project_root/"texture.png";
    { std::ofstream out(texture_path,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes),sizeof(bytes)); }
    auto* texture_input=find<bgui::inputbox>(*groups[0],[](auto& input) { return input.has_class("material-texture-path") && input.get_buffer().empty(); });
    ASSERT_NE(texture_input,nullptr);
    texture_input->m_enter_func(texture_path.string());
    EXPECT_EQ(renderer->m_modelo->meshes[0].getMaterial().getTextura("tex_albedo").path,texture_path.string());
    EXPECT_TRUE(renderer->m_modelo->meshes[1].getMaterial().getTextura("tex_albedo").path.empty());
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(renderer->m_modelo->meshes[0].getMaterial().getTextura("tex_albedo").path.empty());
    auto* material_input=find<bgui::inputbox>(*groups[0],[](auto& input) { return input.has_class("material-asset-path"); });
    ASSERT_NE(material_input,nullptr);
    commons::material reusable=renderer->m_modelo->meshes[0].getMaterial();
    reusable.set_uniform("material.roughness",.29f);
    std::filesystem::path reusable_path;
    ASSERT_TRUE(commons::store_material_asset(context->project_root/"materials",reusable,reusable_path,error)) << error;
    material_input->m_enter_func(reusable_path.string());
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].getMaterial().getUniforme("material.roughness").valor.f,.29f);
    ASSERT_TRUE(context->undo());
    // Navigation switches independently from mesh selection and ignores text entry.
    io.m_mouse_position={image->processed_x()+20,image->processed_y()+20};
    bgui::on_update();
    io.m_input_map[bgui::input_key::o]=bgui::input_action::press;
    bgui::on_update();
    EXPECT_TRUE(context->orbital_view);
    bgui::on_update();
    EXPECT_TRUE(context->orbital_view);
    io.m_input_map.clear(); bgui::on_update();
    const auto orbit_eye=panel.preview_camera().position;
    io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::press;
    bgui::on_update();
    io.m_mouse_position.x+=30;
    bgui::on_update();
    EXPECT_NE(panel.preview_camera().position,orbit_eye);
    io.m_input_map.clear(); bgui::on_update();
    io.m_input_map[bgui::input_key::o]=bgui::input_action::press;
    bgui::on_update();
    EXPECT_FALSE(context->orbital_view);
    io.m_input_map.clear(); bgui::on_update();
    const auto normal_eye=panel.preview_camera().position;
    const auto normal_view=panel.preview_camera().viewMatrix;
    io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::press;
    bgui::on_update();
    io.m_mouse_position.x+=30;
    bgui::on_update();
    EXPECT_EQ(panel.preview_camera().position,normal_eye);
    EXPECT_NE(panel.preview_camera().viewMatrix,normal_view);
    io.m_input_map.clear();
    registry->remove<commons::renderer>(second.id);
    bgui::on_update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Nenhum modelo"; }), nullptr);
    std::filesystem::remove_all(context->project_root);
    context->selected_entity.reset();
    bgui::on_update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) { return text.get_buffer() == "Nenhum objeto selecionado"; }), nullptr);
}

TEST(EditorInvisibleControllers, ShortcutsNeverEmitCornerQuads) {
    bgui::scoped_interface ui_scope;
    auto context = std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    editor::ui::undo_redo_shortcuts_system history(context);
    bgui::draw_data data;
    history.style.visual.visible = true;
    history.set_final_rect(0, 0, 100, 100);
    history.get_requires(&data);
    EXPECT_TRUE(data.m_quad_requires.empty());
}

TEST_F(EditorMenuTest, PlayDispatchesAfterClickAndAllowsRetryAfterFailure) {
    std::function<void()> pending;
    editor.set_load_dispatcher([&](auto action) { pending = std::move(action); });
    int calls = 0;
    editor.set_play_state_callback([&](bool playing, std::string& error) {
        EXPECT_TRUE(playing);
        ++calls;
        error = "snapshot failed";
        return false;
    });
    auto* play = find<bgui::button>(bgui::get_layout(), [](auto& item) {
        return item.has_class("runtime-play-button");
    });
    ASSERT_NE(play, nullptr);
    for (int attempt = 1; attempt <= 2; ++attempt) {
        play->on_pressed();
        play->on_released();
        bgui::on_update();
        ASSERT_TRUE(pending);
        EXPECT_EQ(calls, attempt - 1);
        EXPECT_FALSE(play->is_enabled());
        auto action = std::move(pending);
        action();
        EXPECT_EQ(calls, attempt);
        EXPECT_TRUE(play->is_enabled());
    }
}

TEST(EditorProjectTransition, KeepsUnsavedChangesAndReopensPickerAfterDeferredFailure) {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / ("bengine-project-transition-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct cleanup_folder {
        fs::path path;
        ~cleanup_folder() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{folder};
    const auto active = folder / "active.bengine";
    const auto broken = folder / "broken.scene";
    std::ofstream(broken) << "{";

    bgui::scoped_interface ui;
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system persistence(registry, camera.id);
    std::string error;
    ASSERT_TRUE(persistence.save(active.string(), error)) << error;

    auto& root = bgui::get_layout();
    auto& menu = root.add<bgui::menu_bar>(root);
    auto& side = root.add<bgui::linear>();
    auto& dock = root.add<bgui::dock>();
    auto& models = root.add<bgui::dock>();
    auto& management = root.add<bgui::dock>();
    editor::ui::ui_persistence_system layout;
    editor::ui::editor_ui editor(dock, models, management, menu, side, layout, persistence,
        registry, active.string(), folder);
    std::function<void()> pending;
    editor.set_load_dispatcher([&](auto task) { pending = std::move(task); });

    auto* picker = find<bgui::modal>(root, [](auto& modal) {
        return find<bgui::text>(modal, [](auto& text) { return text.get_buffer() == "Project Manager"; });
    });
    ASSERT_NE(picker, nullptr);
    picker->set_enable(true);
    auto* input = find<bgui::inputbox>(*picker, [&](auto& item) { return item.get_buffer() == active.string(); });
    auto* open = find<bgui::button>(*picker, [](auto& item) { return item.get_label().get_buffer() == "Open"; });
    ASSERT_NE(input, nullptr);
    ASSERT_NE(open, nullptr);
    input->set_buffer(broken.string());
    const auto click_open = [&] {
        open->on_pressed();
        open->on_released();
        bgui::on_update();
    };

    registry->create(23);
    click_open();
    EXPECT_FALSE(pending);
    EXPECT_TRUE(registry->is_alive(23));
    EXPECT_TRUE(picker->is_enabled());
    EXPECT_NE(find<bgui::text>(*picker, [](auto& item) {
        return item.get_buffer().find("Save the current project") != std::string::npos;
    }), nullptr);

    ASSERT_TRUE(persistence.save(active.string(), error)) << error;
    for (int attempt = 0; attempt < 2; ++attempt) {
        click_open();
        ASSERT_TRUE(pending);
        EXPECT_FALSE(picker->is_enabled());
        auto load = std::move(pending);
        load();
        EXPECT_TRUE(picker->is_enabled());
        EXPECT_EQ(input->get_buffer(), broken.string());
        EXPECT_TRUE(registry->is_alive(23));
    }
}

TEST(HudEditor, SelectedWidgetPropertiesAppearInInspector) {
    bgui::scoped_interface ui;
    auto registry=std::make_shared<commons::ecs>();
    auto entity=registry->create(); registry->add<commons::hud>(entity);
    auto h=registry->get<commons::hud>(entity.id);
    auto widget=h->add("button","Use");
    auto context=std::make_shared<editor::ui::editor_context>(registry);
    context->select_entity(entity.id); context->selected_hud_widget=widget;
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view inspector(context); inspector.build(dock); inspector.update();
    EXPECT_NE(find<bgui::inputbox>(dock,[&](auto& input){return input.get_buffer()==widget->id;}),nullptr);
    EXPECT_NE(find<bgui::text>(dock,[](auto& text){return text.get_buffer()=="Lua callback";}),nullptr);
    auto* title=find<bgui::inputbox>(dock,[](auto& input){return input.get_buffer()=="Use";});
    ASSERT_NE(title,nullptr); title->m_enter_func("Interact");
    EXPECT_EQ(widget->text,"Interact");
    EXPECT_TRUE(context->undo()); EXPECT_EQ(widget->text,"Use");
    registry->remove(entity.id); EXPECT_NO_THROW(inspector.update());
}

TEST(HudEditor, RightClickAddsWidgetToGameCamera) {
    bgui::scoped_interface ui;
    auto& io=bgui::get_context(); io.m_size={900,600}; io.m_input_map.clear();
    io.m_last_mouse_left=false;
    auto registry=std::make_shared<commons::ecs>();
    auto context=std::make_shared<editor::ui::editor_context>(registry);
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    const auto other = registry->create();
    context->selected_entity = other.id;
    auto& dock=bgui::get_layout().add<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::match_parent);
    editor::ui::game_view panel(context); panel.build(dock);
    for(int i=0;i<3;++i) bgui::on_update();
    auto* image=find<bgui::image>(dock,[](auto& value){return value.has_class("editor-viewport-surface");});
    ASSERT_NE(image,nullptr); ASSERT_GT(image->processed_width(),0);
    io.m_mouse_position={image->processed_x()+80,image->processed_y()+80};
    io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::press;
    bgui::on_update();
    io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::release;
    bgui::on_update(); bgui::on_update();
    EXPECT_EQ(find<editor::ui::action_popup>(bgui::get_layout(), [](auto&) { return true; }), nullptr);
    io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::press;
    bgui::on_update();
    io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::release;
    bgui::on_update();
    auto* option=find<bgui::button>(bgui::get_layout(),[](auto& button){return button.get_label().get_buffer()=="Add text";});
    ASSERT_NE(option,nullptr);
    EXPECT_EQ(find<bgui::button>(bgui::get_layout(), [](auto& button) {
        return button.get_label().get_buffer() == "Add 3D text";
    }), nullptr);
    option->on_pressed(); option->on_released(); bgui::on_update(); bgui::on_update();
    auto widget=context->selected_hud_widget.lock(); ASSERT_NE(widget,nullptr);
    EXPECT_EQ(widget->kind,"text"); EXPECT_TRUE(widget->absolute);
    EXPECT_EQ(context->selected_entity, camera.id);
    ASSERT_TRUE(registry->get<commons::hud>(camera.id));
    EXPECT_FALSE(registry->get<commons::hud>(other.id));
}

TEST_F(EditorMenuTest, UnifiesOnlyIdenticalSceneMaterialsAndKeepsEditsIndependent) {
    const auto add_mesh = [&](const commons::material& material) -> commons::mesh& {
        auto entity = registry->create();
        auto model = std::make_unique<commons::model>();
        model->meshes.emplace_back(std::vector<commons::vertex>{}, std::vector<unsigned>{}, material);
        registry->add<commons::renderer>(entity, model.release());
        return registry->get<commons::renderer>(entity.id)->m_modelo->meshes.front();
    };

    commons::material material;
    material.set_uniform("material.roughness", 0.5f);
    material.set_texture("tex_albedo", commons::texture(0, "albedo.png"));
    auto& first = add_mesh(material);
    auto& duplicate = add_mesh(material);
    material.textures.at("tex_albedo").filtering = commons::texture_filter::trilinear;
    auto& different = add_mesh(material);

    ASSERT_NE(&first.material_configuration(), &duplicate.material_configuration());
    click("Unify Identical Materials");
    EXPECT_EQ(&first.material_configuration(), &duplicate.material_configuration());
    EXPECT_NE(&first.material_configuration(), &different.material_configuration());
    click("OK");

    duplicate.setMaterial(material);
    EXPECT_NE(&first.material_configuration(), &duplicate.material_configuration());
    EXPECT_EQ(first.material_configuration().textures.at("tex_albedo").filtering,
        commons::texture_filter::original);
}

TEST(EditorSceneManagement, CreatesOpensAndDeletesScenesThroughTheWorkspace) {
    namespace fs = std::filesystem;
    const auto folder = fs::temp_directory_path() / ("bengine-scene-workspace-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct cleanup_folder {
        fs::path path;
        ~cleanup_folder() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{folder};

    bgui::scoped_interface ui;
    bgui::get_context().m_size = {1200, 800};
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create(1);
    registry->add<commons::camera>(camera);
    registry->create(22);
    editor::ui::project_persistence_system persistence(registry, camera.id);
    const auto project = folder / "project.bengine";
    std::string error;
    ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    const auto initial_scene = persistence.active_scene();

    auto& root = bgui::get_layout();
    auto& menu = root.add<bgui::menu_bar>(root);
    auto& side = root.add<bgui::linear>();
    auto& dock = root.add<bgui::dock>();
    auto& models = root.add<bgui::dock>();
    auto& management = root.add<bgui::dock>();
    editor::ui::ui_persistence_system layout;
    editor::ui::editor_ui editor(dock, models, management, menu, side, layout, persistence,
        registry, project.string(), folder);
    auto& files = editor.add_panel<editor::ui::files_view>(project);
    int scene_changes = 0;
    editor.setup_scene_management(files, [&] { ++scene_changes; });
    editor.set_workspace_mode(editor::ui::editor_workspace_mode::scene_management);
    bgui::on_update();

    auto* name = find<bgui::inputbox>(management, [](auto& input) {
        return input.has_class("scene-file-name");
    });
    ASSERT_NE(name, nullptr);
    name->set_buffer("Level 2");
    const auto click = [](bgui::element& scope, const std::string& label) {
        auto* button = find<bgui::button>(scope, [&](auto& item) {
            return item.get_label().get_buffer() == label;
        });
        ASSERT_NE(button, nullptr) << label;
        button->on_pressed();
        button->on_released();
        bgui::on_update();
    };
    click(management, "Criar cena");
    ASSERT_TRUE(fs::exists(folder / "Level 2.scene"));
    click(management, "Abrir e tornar inicial");
    EXPECT_EQ(persistence.active_scene(), folder / "Level 2.scene");
    EXPECT_EQ(scene_changes, 1);
    EXPECT_FALSE(registry->is_alive(22));

    click(management, "Excluir...");
    EXPECT_TRUE(fs::exists(initial_scene));
    click(root, editor::ui::tr("Cancelar"));
    EXPECT_TRUE(fs::exists(initial_scene));
    click(management, "Excluir...");
    click(root, "Excluir definitivamente");
    EXPECT_FALSE(fs::exists(initial_scene));
    EXPECT_TRUE(fs::exists(persistence.active_scene()));
}

TEST(EditorProjectManagerTest, MainManagerCallbackPreservesUnsavedProjectGuard) {
    bgui::scoped_interface scope;
    auto registry = std::make_shared<commons::ecs>();
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system project(registry, camera.id);
    const auto directory = std::filesystem::temp_directory_path() /
        ("bengine-manager-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    const auto path = directory / "project.bengine";
    std::string error;
    ASSERT_TRUE(project.save(path.string(), error)) << error;
    auto& root = bgui::get_layout().add<bgui::linear>();
    auto& menu = root.add<bgui::menu_bar>(root);
    auto& side = root.add<bgui::linear>();
    auto& dock = root.add<bgui::dock>();
    auto& model = root.add<bgui::dock>();
    auto& management = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::editor_ui ui(dock, model, management, menu, side, ui_persistence,
        project, registry, path.string(), {});
    int opened = 0;
    ui.set_project_manager_callback([&] { ++opened; });
    auto* button = find<bgui::button>(root, [](auto& item) {
        return item.get_label().get_buffer() == "Project Manager";
    });
    ASSERT_NE(button, nullptr);
    button->on_released();
    bgui::on_update();
    EXPECT_EQ(opened, 1);
    auto entity = registry->create();
    registry->get<commons::transform>(entity.id)->position.x = 2;
    button->on_released();
    bgui::on_update();
    EXPECT_EQ(opened, 1);
    std::filesystem::remove_all(directory);
}

TEST_F(EditorMenuTest, DedicatedScriptingModeKeepsScenePanelsSeparateAndUsesSidebarButton) {
    const auto folder=std::filesystem::temp_directory_path()/("bengine-scripting-mode-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);std::ofstream(folder/"logic.lua")<<"function update()\nend\n";
    editor.set_loaded_project_path((folder/"project.bengine").string());
    auto& files=editor.add_panel<editor::ui::files_view>(folder);editor.setup_scene_management(files,[]{});
    editor.set_workspace_mode(editor::ui::editor_workspace_mode::scene_management);bgui::on_update();
    EXPECT_NE(find<bgui::window>(management_dock,[](auto& w){return w.get_title().get_buffer()=="Conexões entre cenas";}),nullptr);
    EXPECT_EQ(find<bgui::window>(management_dock,[](auto& w){return w.get_title().get_buffer()=="Script em blocos" || w.get_title().get_buffer()=="Scripts do projeto";}),nullptr);
    auto* scripting=find<bgui::dock>(root,[](auto& d){return d.id=="visual-scripting-workspace";});ASSERT_TRUE(scripting);
    auto* button=find<bgui::button>(side_bar,[](auto& b){return b.id=="workspace-mode-3";});ASSERT_TRUE(button);
    button->on_pressed();button->on_released();bgui::on_update();bgui::on_update();
    EXPECT_TRUE(scripting->is_enabled());EXPECT_FALSE(management_dock.is_enabled());EXPECT_EQ(context->workspace_mode,3);
    EXPECT_NE(find<bgui::window>(*scripting,[](auto& w){return w.get_title().get_buffer()=="Script em blocos";}),nullptr);
    EXPECT_EQ(find<bgui::window>(*scripting,[](auto& w){return w.get_title().get_buffer()=="Conexões entre cenas";}),nullptr);
    std::filesystem::remove_all(folder);
}

#include "opengl_test_support.hpp"
#include "os/window.hpp"
#include "systems/ui_system.hpp"
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
TEST(EditorScriptingNative, RendersDedicatedWorkspaceAndOneFullSizedDragCard) {
    if(!test_support::supports_opengl33()) GTEST_SKIP()<<"OpenGL 3.3 unavailable";
    const auto folder=std::filesystem::temp_directory_path()/("bengine-native-scripting-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    std::ofstream(folder/"movement.lua")<<"function setup()\n Debug.Log(\"Pronto\")\nend\nfunction update()\n gameObject.transform:Translate(Vector3(1, 0, 0))\n if Input.GetKey(\"Space\") then\n  SceneManager.LoadScene(\"next.scene\")\n end\nend\n";
    std::ofstream(folder/"main.scene")<<R"({"entities":[{"components":{"code":{"directory":"movement.lua"}}}]})";
    std::ofstream(folder/"next.scene")<<R"({"entities":[]})";
    commons::window window("Bubble Engine - Visual scripting",1600,1000);window.add<commons::ui_system>();
    auto registry=std::make_shared<commons::ecs>();auto context=std::make_shared<editor::ui::editor_context>(registry);
    editor::ui::project_persistence_system persistence(registry,0);editor::ui::ui_persistence_system layout;
    auto& root=bgui::get_layout().add<bgui::linear>(bgui::orientation::horizontal);
    root.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::match_parent);
    auto& side=root.add<bgui::linear>(bgui::orientation::vertical);side.style.layout.require_mode(bgui::mode::pixel,bgui::mode::match_parent);side.style.layout.require_width(bgui::mode::pixel,44);
    auto& center=root.add<bgui::linear>(bgui::orientation::vertical);center.style.layout.require_mode(bgui::mode::stretch,bgui::mode::match_parent);
    auto& menu=center.add<bgui::menu_bar>(root);auto& dock=center.add<bgui::dock>();auto& models=center.add<bgui::dock>();auto& scenes=center.add<bgui::dock>();
    for(auto* surface:{&dock,&models,&scenes}) surface->style.layout.require_mode(bgui::mode::match_parent,bgui::mode::match_parent);
    editor::ui::editor_ui editor(dock,models,scenes,menu,side,layout,persistence,registry,(folder/"project.bengine").string(),{});
    editor.set_editor_context(context);
    auto& files=editor.add_panel<editor::ui::files_view>(folder);editor.setup_scene_management(files,[]{});
    editor.set_workspace_mode(editor::ui::editor_workspace_mode::visual_scripting);
    auto* scripting=find<bgui::dock>(center,[](auto& d){return d.id=="visual-scripting-workspace";});ASSERT_TRUE(scripting);
    const auto frame=[&](bool native=true) {
        if(native) bgui::glfw_update(bgui::get_context());bgui::load_font_queue();editor.update();bgui::on_update();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1600,1000);glDisable(GL_FRAMEBUFFER_SRGB);bgui::gl3_clear();bgui::gl3_render(bgui::get_draw_data());
    };
    const auto capture=[&](const char* name) {
        if(const auto* directory=std::getenv("BENGINE_SCRIPTING_SCREENSHOTS")) {
            std::filesystem::create_directories(directory);
            std::vector<unsigned char> pixels(1600*1000*3);glReadPixels(0,0,1600,1000,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            std::ofstream output(std::filesystem::path(directory)/(std::string(name)+".ppm"),std::ios::binary);output<<"P6\n1600 1000\n255\n";
            for(int y=999;y>=0;--y) output.write(reinterpret_cast<const char*>(pixels.data()+y*1600*3),1600*3);
        }
    };
    frame();frame();
    auto* script=find<bgui::button>(*scripting,[](auto& b){return b.get_label().get_buffer()=="movement.lua";});ASSERT_TRUE(script);
    script->on_pressed();script->on_released();frame();frame();
    auto* categories=find<bgui::linear>(*scripting,[](auto& e){return e.type=="visual-script-categories";});ASSERT_TRUE(categories);
    auto* palette=find<bgui::linear>(*scripting,[](auto& e){return e.type=="visual-script-palette";});ASSERT_TRUE(palette);
    auto* tree=find<bgui::linear>(*scripting,[](auto& e){return e.type=="visual-script-tree";});ASSERT_TRUE(tree);
    EXPECT_GT(palette->processed_x(),categories->processed_x());EXPECT_GT(tree->processed_x(),palette->processed_x());
    EXPECT_GT(tree->processed_width(),600);EXPECT_GT(tree->processed_height(),500);
    EXPECT_FALSE(scenes.is_enabled());capture("03-scripting-workspace");
    auto* mover=find<bgui::button>(*scripting,[](auto& b){return b.get_label().get_buffer()==":: Mover";});ASSERT_TRUE(mover);
    auto& io=bgui::get_context();io.m_mouse_position={mover->processed_x()+30,mover->processed_y()+10};mover->on_pressed();
    io.m_mouse_position={1250,430};mover->set_drag({100,100});
    io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::press;io.m_last_mouse_left=true;
    frame(false);frame(false);
    auto* preview=find<bgui::element>(bgui::get_layout(),[](auto& e){return e.type=="visual-block-drag-preview";});ASSERT_TRUE(preview);
    EXPECT_TRUE(preview->is_enabled());EXPECT_GT(preview->processed_width(),300);EXPECT_GT(preview->processed_height(),60);
    bgui::draw_data data;data.m_clip_rect={0,0,1600,1000};preview->get_requires(&data);int cards=0;
    while(!data.m_quad_requires.empty()) {
        const auto r=data.m_quad_requires.front().m_rect;data.m_quad_requires.pop();
        if(int(r.z)==preview->processed_width() && int(r.w)==preview->processed_height()) ++cards;
    }
    EXPECT_EQ(cards,1);capture("04-drag-preview");
    io.m_mouse_position={-100,-100};mover->on_released();io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::release;frame(false);
    EXPECT_FALSE(preview->is_enabled());
    editor.set_workspace_mode(editor::ui::editor_workspace_mode::scene_management);frame();frame();
    EXPECT_TRUE(scenes.is_enabled());EXPECT_FALSE(scripting->is_enabled());capture("05-scenes-only");
    auto* graph=find<bgui::element>(scenes,[](auto& e){return e.type=="scene-connection-canvas";});ASSERT_TRUE(graph);
    EXPECT_EQ(find<bgui::window>(scenes,[](auto& w){return w.get_title().get_buffer()=="Script em blocos";}),nullptr);
    auto* node=find<bgui::button>(*graph,[](auto& b){return b.get_label().get_buffer()=="main.scene";});ASSERT_TRUE(node);
    node->on_pressed();node->on_released();frame();frame();
    auto* link=find<bgui::button>(scenes,[](auto& b){return b.get_label().get_buffer()=="Script: movement.lua";});ASSERT_TRUE(link);
    link->on_pressed();link->on_released();frame();frame();
    EXPECT_TRUE(scripting->is_enabled());EXPECT_EQ(context->workspace_mode,3);capture("06-return-to-script");
    auto* properties=find<bgui::button>(*categories,[](auto& b){return b.get_label().get_buffer()=="Propriedades";});ASSERT_TRUE(properties);
    properties->on_pressed();properties->on_released();frame();frame();capture("07-properties");
    auto* events=find<bgui::button>(*categories,[](auto& b){return b.get_label().get_buffer()=="Eventos";});ASSERT_TRUE(events);
    events->on_pressed();events->on_released();frame();frame();capture("08-events");
    EXPECT_EQ(glGetError(),GL_NO_ERROR);
    std::filesystem::remove_all(folder);
}
