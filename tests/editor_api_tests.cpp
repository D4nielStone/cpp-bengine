#include "editor_api.hpp"
#include "editor_commands.hpp"
#include "components/code.hpp"

#include <gtest/gtest.h>

using namespace editor::ui;

TEST(EditorApi, GroupingIsOneUndoableSceneEdit) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor_context>(registry);
    editor_api api(context);
    editor_commands commands(context);
    const auto first = api.create_entity();
    const auto second = api.create_entity();
    ASSERT_TRUE(first && second);
    api.select_entity(first);
    api.select_entity(second, true);
    api.group_selected_entities();
    const auto group = api.selected_entity();
    ASSERT_TRUE(group);
    EXPECT_EQ(api.parent_of(*first), *group);
    EXPECT_EQ(api.parent_of(*second), *group);
    EXPECT_EQ(context->undo_stack.size(), 3u);
    const auto revision = api.events().revision(editor_event::scene);
    ASSERT_TRUE(commands.execute(editor_command::undo));
    EXPECT_FALSE(registry->is_alive(*group));
    EXPECT_EQ(api.parent_of(*first), 0u);
    EXPECT_EQ(api.parent_of(*second), 0u);
    EXPECT_GT(api.events().revision(editor_event::scene), revision);
    ASSERT_TRUE(commands.execute(editor_command::redo));
    EXPECT_EQ(api.parent_of(*first), *group);
    EXPECT_EQ(api.parent_of(*second), *group);
}

TEST(EditorApi, AssetSelectionAndPropertyHistoryPublishTheirEvents) {
    auto context = std::make_shared<editor_context>(std::make_shared<commons::ecs>());
    editor_api api(context);
    editor_commands commands(context);
    auto id = api.create_entity();
    ASSERT_TRUE(id);
    api.select_entity(id);
    const auto selection = api.events().revision(editor_event::selection);
    api.select_asset(editor_asset_kind::image, "texture.png");
    EXPECT_FALSE(api.selected_entity());
    EXPECT_EQ(api.selected_asset(editor_asset_kind::image), "texture.png");
    EXPECT_GT(api.events().revision(editor_event::selection), selection);
    const auto assets = api.events().revision(editor_event::assets);
    api.select_entity(id);
    EXPECT_TRUE(api.selected_asset(editor_asset_kind::image).empty());
    EXPECT_GT(api.events().revision(editor_event::assets), assets);

    int value = 1;
    api.record_property_edit([&] { value = 0; }, [&] { value = 1; });
    EXPECT_EQ(api.events().revision(editor_event::properties), 1u);
    ASSERT_TRUE(commands.execute(editor_command::undo));
    EXPECT_EQ(value, 0);
    ASSERT_TRUE(commands.execute(editor_command::redo));
    EXPECT_EQ(value, 1);
    EXPECT_EQ(api.events().revision(editor_event::properties), 3u);
}

TEST(EditorApi, ScriptPropertyEditRejectsDetachedScripts) {
    auto registry = std::make_shared<commons::ecs>();
    auto context = std::make_shared<editor_context>(registry);
    editor_api api(context);
    auto owner = registry->create();
    registry->add<commons::code>(owner);
    auto script = registry->get<commons::code>(owner.id);
    script->properties.push_back({"speed", "number", 1.0});

    const commons::script_property change{"speed", "number", 2.0};
    ASSERT_TRUE(api.set_script_property(owner.id, script, change));
    EXPECT_EQ(std::get<double>(script->properties.front().value), 2.0);
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(std::get<double>(script->properties.front().value), 1.0);
    registry->remove<commons::code>(owner.id);
    EXPECT_FALSE(api.set_script_property(owner.id, script, change));
}
