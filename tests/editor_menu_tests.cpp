#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include "editor_ui.hpp"
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
    EXPECT_EQ(workspace_button_count, 3u);

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
    dock.add_window("Arquivos", bgui::dock_area::right);
    dock.add_window("Debug", bgui::dock_area::right);
    click("Layout: Wide Scene");
    auto configuration = dock.get_configuration();
    EXPECT_FLOAT_EQ(configuration.left_ratio, .16f);
    EXPECT_FLOAT_EQ(configuration.right_ratio, .18f);
    for (const auto& window : configuration.windows) {
        EXPECT_FALSE(window.floating);
        if (window.title == "Scene") EXPECT_EQ(window.area, bgui::dock_area::center);
        if (window.title == "Hierarchy") EXPECT_EQ(window.area, bgui::dock_area::left);
        if (window.title == "Arquivos") EXPECT_EQ(window.area, bgui::dock_area::bottom);
    }
    ASSERT_EQ(configuration.tab_groups.size(), 1u);
    EXPECT_EQ(configuration.tab_groups.front().windows, (std::vector<std::string>{"Arquivos", "Debug"}));
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
    context->workspace_mode = 2;
    context->model_selection = 3;
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
    context->model_selection = 0; context->orbital_view = false; context->model_orbital_view = false;
    context->inspector_sections.clear();
    camera->config.atmosphere_zenith = {};
    ASSERT_TRUE(workspace.load_workspace(files, error)) << error;
    EXPECT_EQ(context->selected_entity, entity.id);
    EXPECT_EQ(context->workspace_mode, 2);
    EXPECT_EQ(context->model_selection, 3);
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

TEST(ModelEditorLayout, KeepsToolsInSidebarBesidePreview) {
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
    EXPECT_GE(extrude->processed_y(), distance->processed_y() + distance->processed_height());
    auto* image = find<bgui::image>(dock, [](auto& image) { return image.has_class("model-editor-preview"); });
    ASSERT_NE(image, nullptr);
    EXPECT_GE(distance->processed_x(), image->processed_x() + image->processed_width());
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
    registry->add<commons::renderer>(first, make_model(1).release());
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
    // Drag the existing gizmo's projected X axis, then save the changed mesh.
    const auto matrix = panel.preview_camera().projMatriz * panel.preview_camera().viewMatrix;
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
    save->on_released();
    bgui::on_update();
    bgui::on_update();
    const auto saved_path = registry->get<commons::renderer>(first.id)->m_modelo->get_source_path();
    EXPECT_TRUE(std::filesystem::exists(saved_path));
    commons::editable_model_data saved;
    std::string error;
    ASSERT_TRUE(commons::load_editable_model(saved_path, saved, error)) << error;
    ASSERT_EQ(saved.meshes.size(), 1u);
    EXPECT_GT(saved.meshes[0].vertices[2].position.x, vertices[2].position.x + .01f);
    EXPECT_NEAR(saved.meshes[0].vertices[2].position.y, vertices[2].position.y, .001f);
    EXPECT_EQ(registry->get<commons::transform>(first.id)->get_position(), (bgui::vec3{}));
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
    auto* texture_input=find<bgui::inputbox>(*groups[0],[](auto& input) { return input.get_buffer().empty(); });
    ASSERT_NE(texture_input,nullptr);
    texture_input->m_enter_func(texture_path.string());
    EXPECT_EQ(renderer->m_modelo->meshes[0].getMaterial().getTextura("tex_albedo").path,texture_path.string());
    EXPECT_TRUE(renderer->m_modelo->meshes[1].getMaterial().getTextura("tex_albedo").path.empty());
    ASSERT_TRUE(context->undo());
    EXPECT_TRUE(renderer->m_modelo->meshes[0].getMaterial().getTextura("tex_albedo").path.empty());
    // Navigation switches independently from mesh selection and ignores text entry.
    io.m_mouse_position={image->processed_x()+20,image->processed_y()+20};
    bgui::on_update();
    io.m_input_map[bgui::input_key::o]=bgui::input_action::press;
    bgui::on_update();
    EXPECT_TRUE(context->model_orbital_view);
    bgui::on_update();
    EXPECT_TRUE(context->model_orbital_view);
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
    EXPECT_FALSE(context->model_orbital_view);
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
