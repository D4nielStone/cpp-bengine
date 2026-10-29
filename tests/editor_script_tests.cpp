#include <gtest/gtest.h>
#include "script_tools.hpp"
#include <chrono>

namespace {
class EditorScriptTest : public testing::Test {
protected:
    std::filesystem::path folder = std::filesystem::temp_directory_path() /
        ("bengine-scripts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::shared_ptr<editor::ui::editor_context> context =
        std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    void SetUp() override {
        std::filesystem::create_directory(folder);
        context->project_root = folder;
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(folder, ignored);
    }
};
}

TEST_F(EditorScriptTest, CreatesTemplateWithoutOverwritingOrEscapingFolder) {
    std::filesystem::path path;
    std::string error;
    ASSERT_TRUE(editor::ui::create_lua_script(folder, "Movement", path, error)) << error;
    EXPECT_EQ(path, folder / "Movement.lua");
    std::ifstream input(path);
    const std::string original{std::istreambuf_iterator<char>(input), {}};
    EXPECT_NE(original.find("function update()"), std::string::npos);
    EXPECT_FALSE(editor::ui::create_lua_script(folder, "Movement.lua", path, error));
    EXPECT_FALSE(editor::ui::create_lua_script(folder, "../escape", path, error));
    EXPECT_FALSE(editor::ui::create_lua_script(folder, "file.cpp", path, error));
    std::ifstream preserved(folder / "Movement.lua");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(preserved), {}), original);
}

TEST_F(EditorScriptTest, AttachIsRelativeUndoableAndDoesNotExecuteUserCode) {
    const auto path = folder / "Movement.lua";
    std::ofstream(path) << "error('must not run while editing')\nfunction update() end\n";
    auto entity = context->registry->create();
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, entity.id, path, error)) << error;
    auto component = context->registry->get<commons::code>(entity.id);
    ASSERT_NE(component, nullptr);
    EXPECT_EQ(component->file, "Movement.lua");
    EXPECT_EQ(component->arquivoCompleto, path.string());
    ASSERT_TRUE(context->undo());
    EXPECT_FALSE(context->registry->has<commons::code>(entity.id));
    ASSERT_TRUE(context->redo());
    EXPECT_EQ(context->registry->get<commons::code>(entity.id), component);
    EXPECT_TRUE(context->registry->validate_invariants());
}

TEST_F(EditorScriptTest, InvalidOrLockedDropPreservesExistingScriptAndHistory) {
    auto entity = context->registry->create();
    const auto valid = folder / "valid.lua";
    const auto invalid = folder / "invalid.lua";
    std::ofstream(valid) << "function update() end";
    std::ofstream(invalid) << "function !!!!";
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, entity.id, valid, error));
    auto previous = context->registry->get<commons::code>(entity.id);
    EXPECT_FALSE(editor::ui::attach_lua_script(context, entity.id, invalid, error));
    EXPECT_FALSE(error.empty());
    context->locked_entities.insert(entity.id);
    EXPECT_FALSE(editor::ui::attach_lua_script(context, entity.id, valid, error));
    EXPECT_EQ(context->registry->get<commons::code>(entity.id), previous);
    EXPECT_EQ(context->undo_stack.size(), 1u);
}

TEST_F(EditorScriptTest, AttachingSecondScriptPreservesFirstAndUndoRestoresList) {
    auto entity = context->registry->create();
    std::ofstream(folder / "first.lua") << "function update() end";
    std::ofstream(folder / "second.lua") << "function update() end";
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, entity.id, "first.lua", error));
    ASSERT_TRUE(editor::ui::attach_lua_script(context, entity.id, "second.lua", error));
    EXPECT_EQ(context->registry->get<commons::code>(entity.id)->file, "first.lua");
}
