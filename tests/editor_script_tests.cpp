#include <gtest/gtest.h>
#include "script_tools.hpp"
#include "systems/code_system.hpp"
#include "debugging/script_log.hpp"
#include <chrono>
#include <limits>

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

TEST_F(EditorScriptTest, ExplicitPropertiesAreReadWithoutExecutingLuaAndArePerEntity) {
    const auto path = folder / "Properties.lua";
    std::ofstream(path) << R"(--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"title","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":"assets/icon.png"}
error('must not run in the editor')
)";
    const auto a = context->registry->create(), b = context->registry->create();
    std::string error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, a.id, path, error)) << error;
    ASSERT_TRUE(editor::ui::attach_lua_script(context, b.id, path, error)) << error;
    const auto first = context->registry->get<commons::code>(a.id);
    const auto second = context->registry->get<commons::code>(b.id);
    ASSERT_EQ(first->properties.size(), 5u);
    EXPECT_TRUE(first->set_property("speed", 12.5));
    EXPECT_DOUBLE_EQ(std::get<double>(second->properties[0].value), 5);
    EXPECT_FALSE(first->set_property("speed", std::string("wrong type")));
    EXPECT_FALSE(first->set_property("speed", std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(first->set_property("tint", std::array<float, 4>{1, 2, 0, 1}));
    EXPECT_TRUE(first->load_properties(error));
    EXPECT_DOUBLE_EQ(std::get<double>(first->properties[0].value), 12.5);
}

TEST_F(EditorScriptTest, PropertyErrorsPreserveExistingValuesAndRejectAttachment) {
    const auto path = folder / "Bad.lua";
    auto entity = context->registry->create();
    for (const auto* metadata : {
        R"({"name":"speed","type":"number","default":"wrong"})",
        R"({"name":"tint","type":"color","default":[2,0,0,1]})",
        R"({"name":"bad name","type":"text","default":""})",
        R"({"name":"x","type":"unknown","default":0})", "{}", "not json"}) {
        std::ofstream(path) << "--@property " << metadata << '\n';
        std::string error;
        EXPECT_FALSE(editor::ui::attach_lua_script(context, entity.id, path, error));
        EXPECT_NE(error.find(":1"), std::string::npos) << error;
        EXPECT_FALSE(context->registry->has<commons::code>(entity.id));
    }
    std::ofstream(path) << "--@property {\"name\":\"x\",\"type\":\"number\",\"default\":1}\n";
    commons::code script(path.string());
    std::string error;
    ASSERT_TRUE(script.load_properties(error));
    const auto before = script.properties;
    std::ofstream(path, std::ios::app) << "--@property {\"name\":\"x\",\"type\":\"number\",\"default\":2}\n";
    EXPECT_FALSE(script.load_properties(error));
    EXPECT_EQ(script.properties, before);
}

TEST_F(EditorScriptTest, PropertiesRoundTripForEveryScriptAndReachLuaBeforeTopLevelAndSetup) {
    const auto path = folder / "Runtime.lua";
    std::ofstream(path) << R"(--@property {"name":"speed","type":"number","default":5}
--@property {"name":"enabled","type":"boolean","default":true}
--@property {"name":"title","type":"text","default":"Player"}
--@property {"name":"tint","type":"color","default":[1,0.5,0,1]}
--@property {"name":"icon","type":"image","default":"assets/icon.png"}
top_level = properties.speed
function setup()
    observed = properties.speed
    assert(properties.enabled and properties.title == "Player")
    assert(type(properties.tint) == "userdata")
    assert(properties.tint.r == 1 and properties.tint.g == 0.5 and properties.tint.a == 1)
end
function update() observed = observed + properties.speed end
)";
    commons::code original(path.string());
    std::string error;
    ASSERT_TRUE(original.load_properties(error));
    ASSERT_TRUE(original.set_property("speed", 12.5));
    auto child = std::make_shared<commons::code>(path.string());
    ASSERT_TRUE(child->load_properties(error));
    ASSERT_TRUE(child->set_property("speed", 42.0));
    original.scripts.push_back(child);
    rapidjson::Document document;
    document.SetObject();
    ASSERT_TRUE(original.serialize(document, document.GetAllocator()));
    commons::code restored;
    ASSERT_TRUE(restored.analyze(document));
    EXPECT_EQ(restored.properties, original.properties);
    ASSERT_EQ(restored.scripts.size(), 1u);
    EXPECT_EQ(restored.scripts[0]->properties, child->properties);
    restored.asset_root = folder.string();
    restored.setup();
    EXPECT_DOUBLE_EQ(restored.estado_lua["top_level"].get<double>(), 12.5);
    EXPECT_DOUBLE_EQ(restored.estado_lua["observed"].get<double>(), 12.5);
    const sol::table properties = restored.estado_lua["properties"];
    EXPECT_EQ(properties["icon"].get<std::string>(), (folder / "assets/icon.png").string());
    restored.update();
    EXPECT_DOUBLE_EQ(restored.estado_lua["observed"].get<double>(), 25);
    restored.scripts[0]->setup();
    EXPECT_DOUBLE_EQ(restored.scripts[0]->estado_lua["observed"].get<double>(), 42);
    // Invalid persisted values must not partially replace an existing component.
    document["script_properties"][0][0]["value"].SetBool(false);
    EXPECT_FALSE(restored.analyze(document));
    EXPECT_EQ(restored.properties, original.properties);
    EXPECT_EQ(restored.scripts[0]->properties, child->properties);
}

TEST_F(EditorScriptTest, LegacyScenesStillLoadAndChangedDeclarationTypesUseNewDefaults) {
    rapidjson::Document legacy;
    legacy.Parse(R"({"directory":"Old.lua","scripts":["Second.lua"]})");
    commons::code script;
    ASSERT_TRUE(script.analyze(legacy));
    EXPECT_TRUE(script.properties.empty());
    ASSERT_EQ(script.scripts.size(), 1u);
    const auto path = folder / "Changed.lua";
    script.arquivoCompleto = path.string();
    script.properties = {{"x", "number", 42.0}, {"removed", "text", std::string("old")}};
    std::ofstream(path) << "--@property {\"name\":\"x\",\"type\":\"text\",\"default\":\"new\"}\n";
    std::string error;
    ASSERT_TRUE(script.load_properties(error));
    ASSERT_EQ(script.properties.size(), 1u);
    EXPECT_EQ(std::get<std::string>(script.properties[0].value), "new");
}

TEST_F(EditorScriptTest, InvalidPropertyMetadataDisablesOnlyTheAffectedRuntimeScript) {
    const auto path = folder / "BadMetadata.lua";
    std::ofstream(path) << "--@property invalid json\nfunction setup() error('must not execute') end\n";
    auto entity = context->registry->create();
    context->registry->add<commons::code>(entity, path.string());
    auto& log = commons::script_log::instance();
    log.clear();
    commons::code_system system;
    EXPECT_NO_THROW(system.setup(context->registry));
    ASSERT_FALSE(log.lines().empty());
    EXPECT_NE(log.lines().back().find("Invalid Lua property"), std::string::npos);
    EXPECT_NO_THROW(system.update(context->registry));
    EXPECT_EQ(log.lines().size(), 1u);
    log.clear();
}
