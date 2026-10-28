#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <gtest/gtest.h>
#include "files_view.hpp"
#include "entities_view.hpp"
#include "entity_picking_system.hpp"
#include "undo_redo_shortcuts_system.hpp"

namespace {
template<class T, class Predicate>
T* find(bgui::element& element, Predicate predicate) {
    if (auto* value = dynamic_cast<T*>(&element); value && predicate(*value)) return value;
    if (auto* layout = element.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* value = find<T>(*child, predicate)) return value;
    return nullptr;
}
class EditorAssetWorkflowTest : public testing::Test {
protected:
    bgui::scoped_interface interface;
    std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("bengine-workflow-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        std::filesystem::create_directory(folder);
        auto& io = bgui::get_context();
        io.m_input_map.clear(); io.m_char_buffer.clear();
        io.m_size = {1000, 700};
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }
};
}

TEST_F(EditorAssetWorkflowTest, CreatesScriptAndUpdatesDirtyAndSaveThroughTheUiLoop) {
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(folder);
    files.build(dock);
    auto* name = find<bgui::inputbox>(dock, [](auto& value) { return value.has_class("new-script-name"); });
    auto* create = find<bgui::button>(dock, [](auto& value) { return value.has_class("new-script-button"); });
    ASSERT_NE(name, nullptr); ASSERT_NE(create, nullptr);
    name->set_buffer("Movement");
    create->on_released();
    bgui::on_update();
    ASSERT_TRUE(std::filesystem::exists(folder / "Movement.lua"));
    auto* window = find<bgui::window>(dock, [](auto& value) {
        return value.get_title().get_buffer() == "Movement.lua";
    });
    ASSERT_NE(window, nullptr);
    auto* input = find<bgui::inputbox>(*window, [](auto&) { return true; });
    ASSERT_NE(input, nullptr);
    const std::string content = "function update()\n    -- edited\nend\n";
    input->set_buffer(content);
    input->set_focused(true);
    bgui::on_update();
    EXPECT_EQ(window->get_title().get_buffer(), "Movement.lua*");
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::s] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map.clear();
    EXPECT_EQ(window->get_title().get_buffer(), "Movement.lua");
    std::ifstream saved(folder / "Movement.lua");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(saved), {}), content);
}

TEST_F(EditorAssetWorkflowTest, LuaFilesDeliverTheirPathWhenDragged) {
    std::ofstream(folder / "Movement.lua") << "function update() end";
    auto& dock = bgui::get_layout().add<bgui::dock>();
    std::filesystem::path dropped;
    editor::ui::files_view files(folder, [&](const auto& path, const auto&) { dropped = path; });
    files.build(dock);
    auto* file = find<bgui::button>(dock, [](auto& value) {
        return find<bgui::text>(value, [](auto& text) { return text.get_buffer() == "Movement.lua"; });
    });
    ASSERT_NE(file, nullptr);
    file->on_pressed();
    file->set_drag({8, 2});
    file->on_released();
    EXPECT_EQ(dropped, folder / "Movement.lua");
}

TEST_F(EditorAssetWorkflowTest, SceneDropFindsTheVisibleEntityWithoutChangingSelection) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    auto camera_entity = registry->create();
    registry->add<commons::camera>(camera_entity);
    auto camera = registry->get<commons::camera>(camera_entity.id);
    camera->viewport({200, 200});
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, new commons::model());
    auto model = registry->get<commons::renderer>(entity.id)->m_modelo;
    model->meshes.emplace_back(std::vector<commons::vertex>{
        {{5, -2, -2}, {1, 0, 0}, {0, 0}},
        {{5, 2, -2}, {1, 0, 0}, {1, 0}},
        {{5, 0, 2}, {1, 0, 0}, {0, 1}}
    }, std::vector<unsigned>{0, 1, 2}, commons::material{});
    auto& image = bgui::get_layout().add<bgui::image>();
    image.set_final_rect(50, 40, 200, 200);
    editor::ui::entity_picking_system picker(camera, image, context, {});
    EXPECT_EQ(picker.pick_at({150, 140}), entity.id);
    EXPECT_FALSE(context->selected_entity);
    context->locked_entities.insert(entity.id);
    EXPECT_FALSE(picker.pick_at({150, 140}));
    context->locked_entities.clear();
    context->hidden_entities.insert(entity.id);
    EXPECT_FALSE(picker.pick_at({150, 140}));
    EXPECT_FALSE(picker.pick_at({1, 1}));
    EXPECT_TRUE(editor::ui::viewport_contains_drop(&image, {150, 140}));
    image.set_enable(false);
    EXPECT_FALSE(editor::ui::viewport_contains_drop(&image, {150, 140}));
}

TEST_F(EditorAssetWorkflowTest, TextUndoDoesNotConsumeSceneHistory) {
    auto context = std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    bool undone = false;
    context->record_action([&]() { undone = true; }, [&]() { undone = false; });
    auto& input = bgui::get_layout().add<bgui::inputbox>("text");
    bgui::focus_keyboard(input);
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::z] = bgui::input_action::press;
    editor::ui::undo_redo_shortcuts_system shortcuts(context);
    shortcuts.on_update();
    EXPECT_FALSE(undone);
    EXPECT_EQ(context->undo_stack.size(), 1u);
}
