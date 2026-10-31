#include <gtest/gtest.h>
#include <glad.h>
#define GLFW_INCLUDE_NONE
#include "components_view.hpp"
#include "asset_file_operations.hpp"
#include "scene_asset_paths.hpp"
#include <chrono>
#include <fstream>

namespace {
template<class T, class Predicate>
T* find_element(bgui::element& root, Predicate predicate) {
    if (auto* item = dynamic_cast<T*>(&root); item && predicate(*item)) return item;
    if (auto* layout = root.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* result = find_element<T>(*child, predicate)) return result;
    return nullptr;
}
class LuaPropertiesInspectorTest : public testing::Test {
protected:
    bgui::scoped_interface ui_scope;
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    std::shared_ptr<editor::ui::editor_context> context =
        std::make_shared<editor::ui::editor_context>(registry);
};
}

TEST_F(LuaPropertiesInspectorTest, InspectorLuaPropertiesSupportTypedFieldsAndUndo) {
    const auto folder = std::filesystem::temp_directory_path() /
        ("bengine-inspector-properties-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    struct cleanup { std::filesystem::path path; ~cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } guard{folder};
    const auto path = folder / "Properties.lua";
    std::ofstream(path) << R"(--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"title","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":""}
error('inspector must never execute this')
)";
    auto entity = registry->create();
    registry->add<commons::code>(entity, path.string());
    auto script = registry->get<commons::code>(entity.id);
    context->selected_entity = entity.id;
    context->project_root = folder;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::components_view panel(context); panel.build(dock); panel.update();
    auto field = [&](const std::string& name) {
        return find_element<bgui::field>(dock, [&](auto& value) { return value.get_label().get_buffer() == name; });
    };
    auto* speed = field("speed"); ASSERT_NE(speed, nullptr);
    speed->get_inputbox().m_enter_func("12.5");
    EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 12.5);
    ASSERT_TRUE(context->undo()); EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 5);
    ASSERT_TRUE(context->redo()); EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 12.5);
    speed->get_inputbox().m_enter_func("nan");
    EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 12.5);
    auto* title = field("title"); ASSERT_NE(title, nullptr);
    title->get_inputbox().m_enter_func("Enemy");
    EXPECT_EQ(std::get<std::string>(script->properties[2].value), "Enemy");
    ASSERT_TRUE(context->undo()); EXPECT_EQ(std::get<std::string>(script->properties[2].value), "Player");
    auto* enabled = find_element<bgui::checkbox>(dock, [](auto& value) { return value.get_label().get_buffer() == "enabled"; });
    ASSERT_NE(enabled, nullptr); enabled->on_clicked();
    EXPECT_FALSE(std::get<bool>(script->properties[1].value));
    ASSERT_TRUE(context->undo()); EXPECT_TRUE(std::get<bool>(script->properties[1].value));
    auto* picker = find_element<bgui::color_picker>(dock, [](auto&) { return true; });
    ASSERT_NE(picker, nullptr);
    picker->set_color(bgui::color{0, 0, 1, 0.5f});
    EXPECT_EQ((std::get<std::array<float, 4>>(script->properties[3].value)), (std::array<float, 4>{0, 0, 1, .5f}));
    ASSERT_TRUE(context->undo());
    EXPECT_EQ((std::get<std::array<float, 4>>(script->properties[3].value)), (std::array<float, 4>{1, .5f, 0, 1}));
    auto* alpha = field("tint Alpha"); ASSERT_NE(alpha, nullptr);
    alpha->get_inputbox().m_enter_func("0.25");
    EXPECT_EQ((std::get<std::array<float, 4>>(script->properties[3].value)), (std::array<float, 4>{1, .5f, 0, .25f}));
    ASSERT_TRUE(context->undo());
    auto* icon = field("icon"); ASSERT_NE(icon, nullptr);
    std::ofstream(folder / "icon.PNG") << "test placeholder";
    icon->get_inputbox().m_enter_func((folder / "icon.PNG").string());
    EXPECT_EQ(std::get<std::string>(script->properties[4].value), "icon.PNG");
    ASSERT_TRUE(context->undo()); EXPECT_EQ(std::get<std::string>(script->properties[4].value), "");
    icon->get_inputbox().set_final_rect(400, 500, 100, 20);
    ASSERT_TRUE(panel.drop_asset(folder / "icon.PNG", {450, 510}));
    EXPECT_EQ(std::get<std::string>(script->properties[4].value), "icon.PNG");
    ASSERT_TRUE(context->undo());
    icon->get_inputbox().m_enter_func(path.string());
    EXPECT_EQ(std::get<std::string>(script->properties[4].value), "");
    context->locked_entities.insert(entity.id);
    speed->get_inputbox().m_enter_func("20");
    EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 12.5);
    context->locked_entities.clear(); context->is_playing = true;
    speed->get_inputbox().m_enter_func("30");
    EXPECT_DOUBLE_EQ(std::get<double>(script->properties[0].value), 12.5);
}

TEST(LuaPropertyAssets, MovingImagesUpdatesReferencesAndPreservesTextValues) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor::ui::editor_context>(registry);
    context->project_root = std::filesystem::temp_directory_path();
    auto entity = registry->create();
    registry->add<commons::code>(entity, "Player.lua");
    auto script = registry->get<commons::code>(entity.id);
    script->properties = {{"icon", "image", std::string("icon.png")}, {"title", "text", std::string("icon.png")}};
    auto child = std::make_shared<commons::code>("Child.lua");
    child->properties = script->properties;
    script->scripts.push_back(child);
    editor::ui::relocate_scene_asset(context, context->project_root / "icon.png", context->project_root / "moved.png");
    EXPECT_EQ(std::get<std::string>(script->properties[0].value), "moved.png");
    EXPECT_EQ(std::get<std::string>(child->properties[0].value), "moved.png");
    EXPECT_EQ(std::get<std::string>(script->properties[1].value), "icon.png");
    rapidjson::Document document;
    document.SetObject();
    script->properties[0].value = std::string("icon.png");
    ASSERT_TRUE(script->serialize(document, document.GetAllocator()));
    EXPECT_TRUE(editor::ui::asset_files::rewrite_paths(document, document.GetAllocator(),
        context->project_root, context->project_root / "icon.png", context->project_root / "moved.png"));
    EXPECT_STREQ(document["properties"][0]["value"].GetString(), "moved.png");
    EXPECT_STREQ(document["properties"][1]["value"].GetString(), "icon.png");
}
