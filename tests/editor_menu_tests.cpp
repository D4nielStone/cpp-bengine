#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include "editor_ui.hpp"
#include "undo_redo_shortcuts_system.hpp"
#include <elem/button.hpp>
#include <elem/checkbox.hpp>
#include <elem/inputbox.hpp>
#include <components/camera.hpp>
#include <components/transform.hpp>
#include <chrono>
#include <cstdlib>
#include <functional>

namespace {
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
    bgui::dock& dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::project_persistence_system persistence{registry, 0};
    editor::ui::editor_ui editor{dock, menu, ui_persistence, persistence,
        registry, "project.bengine", "/tmp/bengine-build"};

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

TEST_F(EditorMenuTest, SavedProjectExitsWithoutOpeningSaveConfirmation) {
    auto camera = registry->create();
    registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system project(registry, camera.id);
    const auto path = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) /
        ("bengine-exit-" + std::to_string(std::chrono::steady_clock::now()
            .time_since_epoch().count()) + ".bengine");
    std::string error;
    ASSERT_TRUE(project.save(path.string(), error)) << error;
    editor::ui::editor_ui ui(dock, menu, ui_persistence, project,
        registry, path.string(), "/tmp/bengine-build");
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
    auto& dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui;
    editor::ui::project_persistence_system persistence(registry, camera.id);
    editor::ui::editor_ui editor(dock, menu, ui, persistence, registry,
        project.string(), temporary / "build");
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
    auto& dock = root.add<bgui::dock>();
    editor::ui::ui_persistence_system ui_persistence;
    editor::ui::project_persistence_system persistence(registry, 0);
    editor::ui::editor_ui editor(dock, menu, ui_persistence, persistence, registry, "other.bengine", folder);
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
