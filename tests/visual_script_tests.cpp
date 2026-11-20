#include <algorithm>
#include <gtest/gtest.h>
#include "apps/editor/services/visual_lua.hpp"
#include "apps/editor/services/scene_graph.hpp"
#include "apps/editor/systems/script_templates.hpp"
#include "project_file_transaction.hpp"
#include <chrono>
#include <fstream>
#include <lua.hpp>

namespace vl=editor::ui::visual_lua;
TEST(VisualLua, PreservesTemplatesAndArbitraryLuaExactly) {
    for(const auto& preset:editor::ui::lua_script_templates) {
        const std::string source(preset.source);
        auto blocks=vl::parse(source);
        EXPECT_EQ(vl::emit(blocks),source) << preset.name;
        std::string error; EXPECT_TRUE(vl::validate(vl::emit(blocks),error)) << error;
    }
    for(const std::string source:{
        "--[=[\nfunction fake()\nend\n]=]\nfunction update()\n local s = [==[end\nif true then]==]\nend\n",
        "local f = function()\n return {a=1, b=2}\nend\nreturn f()",
        "-- comentário\r\nfunction setup()\r\n  print(\"olá\\n\") -- preserve\r\nend",
        "local n =\n  1 +\n  2\n",
        "function broken(\n -- incomplete input"}) {
        EXPECT_EQ(vl::emit(vl::parse(source)),source);
    }
}
TEST(VisualLua, NestedControlFlowHasEditableSlotsAndCompilesWithoutExecution) {
    const std::string source="function update(dt)\n  if dt > 0 then -- test\n    for i = 1, 3 do\n      Debug.Log(i)\n    end\n  elseif dt < 0 then\n    return\n  else\n    repeat\n      dt = dt + 1\n    until dt > 0\n  end\nend\n";
    vl::document doc; doc.load(source);
    ASSERT_EQ(doc.blocks().size(),1u);
    const auto& function=doc.blocks()[0]; EXPECT_EQ(function.type,vl::kind::function);
    ASSERT_EQ(function.children.size(),1u);
    const auto id=function.children[0].id;
    ASSERT_TRUE(doc.edit(id,0,"dt >= 1"));
    EXPECT_NE(doc.source().find("if dt >= 1 then -- test"),std::string::npos);
    std::string error; EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    EXPECT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),source);
    EXPECT_TRUE(doc.redo()); EXPECT_NE(doc.source(),source);
    // Loading/validating source must never execute it.
    EXPECT_TRUE(vl::validate("error('do not execute')",error));
}
TEST(VisualLua, MovesWholeSubtreesRejectsCyclesAndRestoresBytesOnUndo) {
    const std::string source="function setup()\n local x = 1\nend\nfunction update()\n Debug.Log(2)\nend";
    vl::document doc; doc.load(source);
    const auto first=doc.blocks()[0].id, second=doc.blocks()[1].id;
    const auto assignment=doc.blocks()[0].children[0].id;
    EXPECT_FALSE(doc.move(first,assignment));
    EXPECT_FALSE(doc.move(first,first));
    EXPECT_FALSE(doc.move(first,second,999));
    ASSERT_TRUE(doc.move(assignment,second));
    EXPECT_TRUE(doc.blocks()[0].children.empty());
    EXPECT_EQ(doc.blocks()[1].children.size(),2u);
    std::string error; EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    ASSERT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),source);
    EXPECT_FALSE(doc.insert(0,vl::kind::branch));
    ASSERT_TRUE(doc.insert(second,vl::kind::condition));
    EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
}
TEST(VisualLua, SceneConnectionsIgnoreCommentsAndDecodeLiterals) {
    const auto edges=vl::transitions(R"(
        -- SceneManager.LoadScene("fake.scene")
        local text = [=[SceneManager.LoadScene("fake2.scene")]=]
        SceneManager.LoadScene('levels/one.scene')
        SceneManager.LoadScene("levels/\x74wo.scene")
        SceneManager.LoadScene([=[levels/three.scene]=])
        SceneManager.LoadScene(prefix .. name)
        SceneManager.ReloadScene()
        function SceneManager.LoadScene(name) end
    )");
    ASSERT_EQ(edges.size(),5u);
    EXPECT_EQ(edges[0].target,"levels/one.scene"); EXPECT_EQ(edges[1].target,"levels/two.scene");
    EXPECT_EQ(edges[2].target,"levels/three.scene"); EXPECT_TRUE(edges[3].dynamic); EXPECT_TRUE(edges[4].reload);
}
class SceneGraphTest:public testing::Test {
protected:
    std::filesystem::path root=std::filesystem::temp_directory_path()/("bengine-scene-graph-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {std::filesystem::create_directories(root/"scripts");std::filesystem::create_directories(root/"levels");}
    void TearDown() override {std::filesystem::remove_all(root);}
    void write(const std::filesystem::path& p,const std::string& text){std::ofstream(root/p)<<text;}
};
TEST_F(SceneGraphTest, ConnectsScenesThroughTheirScriptsAndRetainsIsolatedScenes) {
    write("levels/a.scene",R"({"entities":[{"components":{"code":{"directory":"scripts/travel.lua","properties":{"text":"scripts/unused.lua"}}}}]})");
    write("levels/b.scene",R"({"entities":[]})"); write("levels/c.scene",R"({"entities":[]})");
    write("scripts/travel.lua","SceneManager.LoadScene('levels/b.scene')\nSceneManager.LoadScene(name)\nSceneManager.LoadScene('../outside.scene')");
    write("scripts/unused.lua","-- unassigned script");
    const auto graph=editor::ui::read_scene_graph(root);
    ASSERT_EQ(graph.nodes.size(),3u); ASSERT_EQ(graph.edges.size(),3u);
    EXPECT_EQ(graph.edges[0].from,0u); EXPECT_EQ(graph.edges[0].to,1u); EXPECT_FALSE(graph.edges[0].unresolved);
    EXPECT_TRUE(graph.edges[1].unresolved); EXPECT_TRUE(graph.edges[2].unresolved);
    EXPECT_EQ(editor::ui::project_scripts(root).size(),2u);
    const auto draft=editor::ui::read_scene_graph(root,[&](const auto& p){return p.extension()==".lua"?"SceneManager.ReloadScene()":editor::ui::project_files::read(p);});
    ASSERT_EQ(draft.edges.size(),1u); EXPECT_EQ(draft.edges[0].from,draft.edges[0].to);
}

TEST(VisualLua, CatalogCommandsHaveSafeEditableFieldsAndOneUndoableInsertion) {
    const auto& catalog=vl::commands();
    ASSERT_GT(catalog.size(),150u);
    for(std::size_t i=0;i<catalog.size();++i) {
        SCOPED_TRACE(catalog[i].title);
        std::string error;
        ASSERT_TRUE(vl::validate(catalog[i].source,error)) << error;
        vl::document doc;doc.load("function setup()\r\nend\r\n");
        const auto original=doc.source(); const auto parent=doc.blocks()[0].id;
        ASSERT_TRUE(doc.insert_command(parent,i));
        const auto& b=doc.blocks()[0].children.front();
        ASSERT_NE(vl::describe(b),nullptr);
        EXPECT_EQ(vl::title(b),catalog[i].title);
        for(const auto& field:vl::slots(b)) {
            EXPECT_LE(field.begin,field.end);EXPECT_LE(field.end,b.head.size());
        }
        EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
        ASSERT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),original);
        ASSERT_TRUE(doc.redo()); EXPECT_EQ(doc.blocks()[0].children.size(),1u);
    }
}
TEST(VisualLua, MoveExposesTargetAndThreeIndependentCoordinates) {
    const std::string source="  gameObject.transform:Translate(Vector3(1, math.max(2, 3), 4)) -- retain\r\n";
    vl::document doc;doc.load(source);
    const auto id=doc.blocks()[0].id;
    EXPECT_EQ(vl::title(doc.blocks()[0]),"Mover");
    const auto fields=vl::slots(doc.blocks()[0]);
    ASSERT_EQ(fields.size(),4u);
    EXPECT_EQ(fields[0].label,"Alvo");EXPECT_EQ(fields[1].label,"X");
    EXPECT_EQ(fields[2].label,"Y");EXPECT_EQ(fields[3].label,"Z");
    ASSERT_TRUE(doc.edit(id,2,"properties.speed * Time.deltaTime"));
    EXPECT_EQ(doc.source(),"  gameObject.transform:Translate(Vector3(1, properties.speed * Time.deltaTime, 4)) -- retain\r\n");
    EXPECT_TRUE(doc.undo());EXPECT_EQ(doc.source(),source);
}
TEST(VisualLua, ArgumentsRespectNestedTablesLiteralsAndVectorExpressions) {
    vl::document doc;
    doc.load("Debug.Log({text = 'a,b', value = call(1, 2)}) -- keep\n");
    auto fields=vl::slots(doc.blocks()[0]);ASSERT_EQ(fields.size(),1u);
    EXPECT_EQ(doc.blocks()[0].head.substr(fields[0].begin,fields[0].end-fields[0].begin),"{text = 'a,b', value = call(1, 2)}");
    doc.load("other.transform:Translate(direction * Time.deltaTime)\n");
    fields=vl::slots(doc.blocks()[0]);ASSERT_EQ(fields.size(),2u);
    EXPECT_EQ(vl::title(doc.blocks()[0]),"Mover");
    EXPECT_TRUE(doc.edit(doc.blocks()[0].id,1,"Vector3(1, 2, 3)"));
    EXPECT_EQ(vl::slots(doc.blocks()[0]).size(),4u);
}
TEST(VisualLua, CatalogInsertionRejectsInvalidDestinationsWithoutChangingDocument) {
    vl::document doc;doc.load("Debug.Log(1)\n");const auto source=doc.source();
    EXPECT_FALSE(doc.insert_command(0,vl::commands().size()));
    EXPECT_FALSE(doc.insert_command(doc.blocks()[0].id,0));
    EXPECT_FALSE(doc.insert_command(0,0,999));EXPECT_EQ(doc.source(),source);
    EXPECT_TRUE(doc.insert_command(0,0,doc.blocks()[0].id));
    EXPECT_EQ(vl::title(doc.blocks()[0]),"Mover");
    EXPECT_TRUE(doc.undo());EXPECT_EQ(doc.source(),source);
}
TEST(VisualLua, VectorExpressionsWithTrailingCallsStayAsOneValue) {
    vl::document doc;doc.load("transform:Translate(Vector3(1, 2, 3) * factor())\n");
    const auto fields=vl::slots(doc.blocks()[0]);ASSERT_EQ(fields.size(),2u);
    EXPECT_EQ(doc.blocks()[0].head.substr(fields[1].begin,fields[1].end-fields[1].begin),"Vector3(1, 2, 3) * factor()");
}
TEST(VisualLua, ReadOnlyPropertiesNeverOfferSetterBlocks) {
    for(const std::string name:{"GameObject.id","GameObject.transform","Camera.viewportWidth","Camera.viewportHeight","Camera.forward","Camera.aspect","SpotLight.worldPosition","SpotLight.worldDirection","SoundPlayer.isPlaying","Animation.time","Animation.isPlaying","Animation.isBlending","HUD.count"}) {
        bool reader=false;
        for(const auto& c:vl::commands()) {
            if(c.title=="Ler "+name) reader=true;
            EXPECT_NE(c.title,"Definir "+name);
        }
        EXPECT_TRUE(reader)<<name;
    }
}
TEST(VisualLua, InspectorPropertyNamesRemainStructuredAfterRenaming) {
    vl::document doc;doc.load("properties.speed = 5 -- keep\n");
    EXPECT_EQ(vl::title(doc.blocks()[0]),"Definir propriedade do script");
    const auto id=doc.blocks()[0].id;
    ASSERT_EQ(vl::slots(doc.blocks()[0]).size(),3u);
    EXPECT_TRUE(doc.edit(id,1,"distance"));
    EXPECT_EQ(vl::title(doc.blocks()[0]),"Definir propriedade do script");
    EXPECT_EQ(doc.source(),"properties.distance = 5 -- keep\n");
}
TEST(VisualLua, ClipboardPastesWholeSubtreesWithFreshIdsAndAtomicHistory) {
    vl::document doc;doc.load("function update()\n  Debug.Log(1)\nend\n");
    const auto original=doc.source();const auto function=doc.blocks()[0].id;
    const auto copy=doc.copy(function);ASSERT_EQ(copy,original);
    ASSERT_TRUE(doc.paste(0,copy));ASSERT_EQ(doc.blocks().size(),2u);
    EXPECT_NE(doc.blocks()[0].id,doc.blocks()[1].id);
    EXPECT_NE(doc.blocks()[0].children[0].id,doc.blocks()[1].children[0].id);
    ASSERT_TRUE(doc.undo());EXPECT_EQ(doc.source(),original);
    ASSERT_TRUE(doc.redo());EXPECT_EQ(doc.source(),original+original);
    const auto before=doc.source();std::string error;
    EXPECT_FALSE(doc.paste(0,"function broken(",0,&error));EXPECT_FALSE(error.empty());
    EXPECT_EQ(doc.source(),before);ASSERT_TRUE(doc.undo());EXPECT_EQ(doc.source(),original);
    EXPECT_FALSE(doc.paste(999,"Debug.Log(2)"));EXPECT_EQ(doc.source(),original);
}
TEST(VisualLua, ClipboardValidatesSyntaxInDestinationContext) {
    vl::document doc;doc.load("while true do\nend\n");
    const auto loop=doc.blocks()[0].id;
    EXPECT_TRUE(doc.paste(loop,"break"));EXPECT_NE(doc.source().find("break\nend"),std::string::npos);
    const auto source=doc.source();EXPECT_FALSE(doc.paste(0,"break"));EXPECT_EQ(doc.source(),source);
    EXPECT_TRUE(doc.undo());EXPECT_EQ(doc.source(),"while true do\nend\n");
}
TEST(VisualLua, ControlPaletteInsertsAtDropPositionAndRejectsDuplicateElse) {
    vl::document doc;doc.load("Debug.Log(1)\n");const auto first=doc.blocks()[0].id;
    ASSERT_TRUE(doc.insert(0,vl::kind::condition,first));EXPECT_EQ(doc.blocks()[0].type,vl::kind::condition);
    const auto condition=doc.blocks()[0].id;ASSERT_TRUE(doc.insert(condition,vl::kind::branch));
    const auto source=doc.source();EXPECT_FALSE(doc.insert(condition,vl::kind::branch));EXPECT_EQ(doc.source(),source);
    EXPECT_FALSE(doc.insert(0,vl::kind::loop,999));EXPECT_EQ(doc.source(),source);
    std::string error;EXPECT_TRUE(vl::validate(doc.source(),error))<<error;
}

TEST(VisualLua, CameraColorCommandStartsWithNeutralGrayAndKeepsEditableFields) {
    const auto& catalog = vl::commands();
    const auto command = std::find_if(catalog.begin(), catalog.end(), [](const auto& value) {
        return value.title == "Definir Camera.backgroundColor";
    });
    ASSERT_NE(command, catalog.end());
    EXPECT_NE(command->source.find("Color(0.16, 0.16, 0.16, 1)"), std::string::npos);
    vl::document document;
    document.load("function setup()\nend\n");
    ASSERT_TRUE(document.insert_command(document.blocks()[0].id, std::size_t(command - catalog.begin())));
    const auto& block = document.blocks()[0].children.front();
    const auto fields = vl::slots(block);
    EXPECT_FALSE(fields.empty());
    std::string error;
    EXPECT_TRUE(vl::validate(document.source(), error)) << error;
}

TEST(VisualLua, GeneralFppHasStructuredEventsConditionsAndEditableMovement) {
    const std::string source(editor::ui::script_template(editor::ui::lua_script_preset::fpp_controller).source);
    vl::document doc; doc.load(source);
    EXPECT_EQ(doc.source(),source);
    int functions=0, conditions=0, assignments=0;
    std::uint64_t movement{};
    auto visit=[&](auto&& self,const std::vector<vl::block>& blocks)->void {
        for(const auto& b:blocks) {
            functions+=b.type==vl::kind::function;
            conditions+=b.type==vl::kind::condition;
            assignments+=b.type==vl::kind::assignment;
            if(b.type==vl::kind::assignment && b.head.find("local movement =")!=std::string::npos) movement=b.id;
            if(b.type==vl::kind::raw) {
                auto tokens=vl::lex(b.head);
                EXPECT_TRUE(std::all_of(tokens.begin(),tokens.end(),[](const auto& t){return t.comment;})) << b.head;
            }
            self(self,b.children);
        }
    };
    visit(visit,doc.blocks());
    EXPECT_EQ(functions,3); EXPECT_GE(conditions,9); EXPECT_GT(assignments,15);
    ASSERT_NE(movement,0u);
    const auto fields=vl::slots(*doc.find(movement));
    auto value=std::find_if(fields.begin(),fields.end(),[](const auto& f){return f.label=="Valor";});
    ASSERT_NE(value,fields.end());
    ASSERT_TRUE(doc.edit(movement,std::distance(fields.begin(),value),"direction * 8"));
    std::string error; EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    EXPECT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),source);
}

TEST(VisualLua, CompactAndMultilineConditionsPreserveBytesAndNesting) {
    for(const std::string source:{
        "function update()\n if not a or\n not b then return end\nend\n",
        "if a then if b then x=1 else x=2 end elseif c then x=3 else x=4 end\n",
        "if a then -- then end\r\n print('else end then')\r\nend\r\n"}) {
        vl::document doc; doc.load(source);
        EXPECT_EQ(doc.source(),source);
        ASSERT_FALSE(doc.blocks().empty());
        EXPECT_NE(doc.blocks()[0].type,vl::kind::raw);
        EXPECT_FALSE(doc.blocks()[0].children.empty());
        std::string error; EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    }
}

TEST(VisualLua, CompactGuardsRemainValidAfterRemovingAndMovingTheirBody) {
    const std::string source="function update()\n if ready then value=1 end\n Debug.Log(value)\nend\n";
    vl::document doc; doc.load(source);
    ASSERT_EQ(doc.blocks().size(),1u);
    const auto& function=doc.blocks()[0];
    ASSERT_GE(function.children.size(),2u);
    const auto& guard=function.children[0];
    ASSERT_EQ(guard.type,vl::kind::condition);
    ASSERT_EQ(guard.children.size(),1u);
    const auto action=guard.children[0].id, parent=function.id;
    ASSERT_TRUE(doc.erase(action));
    std::string error; EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    ASSERT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),source);
    ASSERT_TRUE(doc.move(action,parent));
    EXPECT_TRUE(vl::validate(doc.source(),error)) << error;
    ASSERT_TRUE(doc.undo()); EXPECT_EQ(doc.source(),source);
}

TEST(VisualLua, HudAppearanceAndIdLookupExposeEditableArguments) {
    for(const std::string title:{"Buscar elemento por ID","Adicionar elemento","Definir HudElement.font","Definir HudElement.image","Definir HudElement.shader","Definir HudElement.opacity","Definir HudElement.relative","Definir HudElement.color","Definir HudElement.background","Definir HudElement.callback"}) {
        const auto& commands=vl::commands();
        const auto it=std::find_if(commands.begin(),commands.end(),[&](const auto& c){return c.title==title;});
        ASSERT_NE(it,commands.end())<<title;
        auto blocks=vl::parse(it->source);
        ASSERT_EQ(blocks.size(),1u);
        EXPECT_EQ(vl::describe(blocks[0]),&*it);
        EXPECT_FALSE(vl::slots(blocks[0]).empty())<<title;
    }
}

TEST(VisualLua, GlobalsAndShaderCommandsHaveEditableArguments) {
    unsigned count = 0;
    for (const auto& command : vl::commands()) {
        if (command.category != "Globais" && command.category != "Shaders") continue;
        ++count;
        auto blocks = vl::parse(command.source);
        ASSERT_EQ(blocks.size(), 1u) << command.title;
        EXPECT_EQ(vl::describe(blocks[0]), &command) << command.title;
        const auto fields = vl::slots(blocks[0]);
        for (const auto& label : command.arguments)
            EXPECT_NE(std::find_if(fields.begin(), fields.end(), [&](const auto& field) { return field.label == label; }), fields.end()) << command.title << ": " << label;
        std::string error;
        EXPECT_TRUE(vl::validate(vl::emit(blocks), error)) << error;
    }
    EXPECT_EQ(count, 12u);
}

TEST(VisualLua, AudioTransportCommandsKeepEditableReceiver) {
    unsigned count = 0;
    for (const auto& command : vl::commands()) {
        if (command.category != "Áudio") continue;
        vl::document document;
        document.load(command.source + "\n");
        ASSERT_EQ(document.blocks().size(), 1u);
        EXPECT_EQ(vl::title(document.blocks()[0]), command.title);
        const auto fields = vl::slots(document.blocks()[0]);
        ASSERT_EQ(fields.size(), 1u);
        EXPECT_EQ(fields[0].label, "Alvo");
        EXPECT_TRUE(document.edit(document.blocks()[0].id, 0, "audio"));
        EXPECT_NE(document.source().find("audio:"), std::string::npos);
        ++count;
    }
    EXPECT_EQ(count, 4u);
}
