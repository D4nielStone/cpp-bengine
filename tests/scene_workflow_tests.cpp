#include <glad.h>
#define GLFW_INCLUDE_NONE
#include <gtest/gtest.h>
#include "files_view.hpp"
#include "panel_manager.hpp"
#include "scene_workflow.hpp"
#include <chrono>
#include <array>
#include <elem/combo_box.hpp>

namespace {
template<class T,class F>T* find(bgui::element& root,F accept) {
    if(auto* t=dynamic_cast<T*>(&root);t && accept(*t)) return t;
    if(auto* layout=root.as_layout()) for(auto& [layer,children]:layout->get_elements()) for(auto& c:children) if(auto* t=find<T>(*c,accept)) return t;
    return nullptr;
}
class SceneWorkflowTest:public testing::Test {
protected:
    std::unique_ptr<bgui::scoped_interface> ui;
    std::filesystem::path root=std::filesystem::temp_directory_path()/("bengine-block-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override {
        std::filesystem::create_directories(root);
        if(std::string(testing::UnitTest::GetInstance()->current_test_info()->name())!="NativeGraphAndBlockLayoutRendersInsideDock") {
            ui=std::make_unique<bgui::scoped_interface>();
            bgui::get_context().m_size={1400,900};
        }
        std::ofstream(root/"enemy.lua") << "function update()\n    Debug.Log(1)\nend\n";
        std::ofstream(root/"a.scene") << R"({"entities":[]})";
    }
    void TearDown() override {std::filesystem::remove_all(root);}
};
}
TEST_F(SceneWorkflowTest, SharedTextBufferConflictDetectionAndDraftSession) {
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root);files.build(dock);
    const auto path=root/"enemy.lua";
    const auto source=files.script_source(path);
    const auto next="-- visual draft\n"+source;
    files.edit_script_source(path,source,next);
    EXPECT_TRUE(files.has_document_edits(path));
    EXPECT_EQ(files.script_source(path),next);
    EXPECT_EQ(editor::ui::project_files::read(path),source);
    EXPECT_THROW(files.edit_script_source(path,source,"lost update"),std::runtime_error);
    auto* document=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="enemy.lua*";});
    ASSERT_TRUE(document); dock.remove_window(document); files.update();
    EXPECT_TRUE(files.save_session(root/"session"));
    EXPECT_NE(editor::ui::project_files::read(root/"session").find("visual draft"),std::string::npos);
    std::ofstream(path)<<"-- external change\n";
    EXPECT_THROW(files.save_script_source(path,next),std::runtime_error);
    EXPECT_EQ(files.script_source(path),next);
    EXPECT_EQ(editor::ui::project_files::read(path),"-- external change\n");
    files.discard_script_draft_and_reload(path);
    EXPECT_EQ(files.script_source(path),"-- external change\n");
}
TEST_F(SceneWorkflowTest, BuildsThreePanelsEditsBlocksSavesLuaAndReopensClosedWindows) {
    auto& text_dock=bgui::get_layout().add<bgui::dock>();
    auto& dock=bgui::get_layout().add<bgui::dock>();
    text_dock.set_enable(false);
    editor::ui::files_view files(root);files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    bgui::on_update();
    for(const auto* title:{"Scripts do projeto","Conexões entre cenas","Script em blocos"})
        ASSERT_NE(find<bgui::window>(dock,[&](auto& w){return w.get_title().get_buffer()==title;}),nullptr);
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";});ASSERT_TRUE(script);
    script->on_pressed();script->on_released();bgui::on_update();bgui::on_update();
    auto* field=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Mensagem";});ASSERT_TRUE(field);
    field->get_inputbox().set_buffer("42");
    bgui::on_update();
    EXPECT_NE(files.script_source(root/"enemy.lua").find("Debug.Log(42)"),std::string::npos);
    auto* save=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()==editor::ui::tr("Salvar Lua");});ASSERT_TRUE(save);
    save->on_pressed();save->on_released();bgui::on_update();
    EXPECT_NE(editor::ui::project_files::read(root/"enemy.lua").find("Debug.Log(42)"),std::string::npos);
    auto* window=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Script em blocos";});ASSERT_TRUE(window);
    dock.remove_window(window);show();bgui::on_update();
    EXPECT_NE(find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Mensagem";}),nullptr);
}

TEST_F(SceneWorkflowTest, CategorizedCommandsInsertMoveFieldsAndDragPreviewCancelsOutsideTargets) {
    auto& text_dock=bgui::get_layout().add<bgui::dock>();text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root);files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    bgui::on_update();
    auto* categories=find<bgui::linear>(dock,[](auto& c){return c.type=="visual-script-categories";});ASSERT_TRUE(categories);
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";});ASSERT_TRUE(script);
    script->on_pressed();script->on_released();bgui::on_update();bgui::on_update();
    auto* move=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="Mover";});ASSERT_TRUE(move);
    move->on_pressed();move->on_released();bgui::on_update();bgui::on_update();
    auto* x=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="X";});ASSERT_TRUE(x);
    auto* y=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Y";});ASSERT_TRUE(y);
    auto* z=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Z";});ASSERT_TRUE(z);
    EXPECT_EQ(x->get_parent(),y->get_parent());EXPECT_EQ(y->get_parent(),z->get_parent());
    y->get_inputbox().set_buffer("12");bgui::on_update();
    EXPECT_NE(files.script_source(root/"enemy.lua").find("Translate(Vector3(0, 12, 0))"),std::string::npos);
    const auto source=files.script_source(root/"enemy.lua");
    auto& io=bgui::get_context();io.m_mouse_position={100,100};
    move->on_pressed();io.m_mouse_position={200,150};move->set_drag({100,50});
    auto* preview=find<bgui::element>(bgui::get_layout(),[](auto& e){return e.type=="visual-block-drag-preview";});ASSERT_TRUE(preview);
    EXPECT_TRUE(preview->style.visual.visible.value_or(false));EXPECT_TRUE(preview->input_passthrough());
    io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::press;
    preview->on_update();
    bgui::draw_data draw;preview->get_requires(&draw);
    EXPECT_GE(draw.m_draw_list.get_vertices().size(),6u);
    io.m_mouse_position={-100,-100};move->on_released();
    EXPECT_FALSE(preview->style.visual.visible.value_or(true));EXPECT_EQ(files.script_source(root/"enemy.lua"),source);
    io.m_input_map[bgui::input_key::mouse_left]=bgui::input_action::release;
    auto* properties=find<bgui::button>(*categories,[](auto& b){return b.get_label().get_buffer()=="Propriedades";});ASSERT_TRUE(properties);
    properties->on_pressed();properties->on_released();bgui::on_update();bgui::on_update();
    EXPECT_NE(find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="Definir Transform.position";}),nullptr);
}

TEST_F(SceneWorkflowTest, SelectionAndFoldingKeepWidgetsAndScrollAndShortcutsEditBlocks) {
    std::string script_source="function update()\n";
    for(int i=0;i<45;++i) script_source+=" Debug.Log("+std::to_string(i)+")\n";
    script_source+="end\n";std::ofstream(root/"enemy.lua")<<script_source;
    auto& text_dock=bgui::get_layout().add<bgui::dock>();text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root);files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    bgui::on_update();
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";});ASSERT_TRUE(script);
    script->on_pressed();script->on_released();bgui::on_update();bgui::on_update();
    auto* tree=find<bgui::linear>(dock,[](auto& e){return e.type=="visual-script-tree";});ASSERT_TRUE(tree);
    auto& io=bgui::get_context();io.m_mouse_position={tree->processed_x()+10,tree->processed_y()+10};
    io.m_scroll_delta_y=-8;tree->on_update();bgui::on_update();bgui::on_update();
    const auto scroll=tree->get_scroll_offset();ASSERT_GT(scroll,0);
    auto* title=find<bgui::button>(*tree,[&](auto& b){return b.get_label().get_buffer()==":: Registrar mensagem" && b.processed_y()>tree->processed_y() && b.processed_y()+30<tree->processed_y()+tree->processed_height();});ASSERT_TRUE(title);
    auto* group=title->get_parent()->get_parent();
    auto* field=find<bgui::field>(*group,[](auto& f){return f.get_label().get_buffer()=="Mensagem";});ASSERT_TRUE(field);
    bgui::draw_data before_draw;before_draw.m_clip_rect={0,0,1400,900};group->get_requires(&before_draw);
    title->on_pressed();title->on_released();bgui::on_update();bgui::on_update();
    EXPECT_EQ(tree->get_scroll_offset(),scroll);
    EXPECT_EQ(find<bgui::field>(*group,[](auto& f){return f.get_label().get_buffer()=="Mensagem";}),field);
    bgui::draw_data after_draw;after_draw.m_clip_rect={0,0,1400,900};group->get_requires(&after_draw);
    EXPECT_GT(after_draw.m_draw_list.get_vertices().size(),before_draw.m_draw_list.get_vertices().size());
    auto* toggle=find<bgui::button>(*group,[](auto& b){return b.get_label().get_buffer()=="-";});ASSERT_TRUE(toggle);
    auto* content=field->get_parent();
    toggle->on_pressed();toggle->on_released();bgui::on_update();
    EXPECT_FALSE(content->is_enabled());EXPECT_EQ(tree->get_scroll_offset(),scroll);
    EXPECT_EQ(toggle->get_label().get_buffer(),"+");
    toggle->on_pressed();toggle->on_released();bgui::on_update();
    EXPECT_TRUE(content->is_enabled());EXPECT_EQ(tree->get_scroll_offset(),scroll);
    EXPECT_EQ(find<bgui::field>(*group,[](auto& f){return f.get_label().get_buffer()=="Mensagem";}),field);
    std::string clipboard;io.m_set_clipboard=[&](const auto& value){clipboard=value;};io.m_get_clipboard=[&]{return clipboard;};
    const auto key=[&](bgui::input_key value,bool control=true) {
        bgui::focus_keyboard(*tree);io.m_input_map.clear();
        if(control) io.m_input_map[bgui::input_key::left_control]=bgui::input_action::press;
        io.m_input_map[value]=bgui::input_action::press;bgui::on_update();
        io.m_input_map.clear();bgui::on_update();
    };
    const auto source=files.script_source(root/"enemy.lua");
    key(bgui::input_key::c);ASSERT_FALSE(clipboard.empty());
    key(bgui::input_key::v);EXPECT_NE(files.script_source(root/"enemy.lua"),source);
    EXPECT_GT(tree->get_scroll_offset(),0);
    key(bgui::input_key::z);EXPECT_EQ(files.script_source(root/"enemy.lua"),source);
    key(bgui::input_key::y);EXPECT_NE(files.script_source(root/"enemy.lua"),source);
    key(bgui::input_key::s);EXPECT_EQ(editor::ui::project_files::read(root/"enemy.lua"),files.script_source(root/"enemy.lua"));
    title=find<bgui::button>(*tree,[](auto& b){return b.get_label().get_buffer()==":: Registrar mensagem";});ASSERT_TRUE(title);
    title->on_pressed();title->on_released();bgui::on_update();
    const auto prior_delete=files.script_source(root/"enemy.lua");
    key(bgui::input_key::delete_key,false);EXPECT_NE(files.script_source(root/"enemy.lua"),prior_delete);
    key(bgui::input_key::z);EXPECT_EQ(files.script_source(root/"enemy.lua"),prior_delete);
    title=find<bgui::button>(*tree,[](auto& b){return b.get_label().get_buffer()==":: Registrar mensagem";});ASSERT_TRUE(title);
    title->on_pressed();title->on_released();bgui::on_update();
    key(bgui::input_key::d);EXPECT_NE(files.script_source(root/"enemy.lua"),prior_delete);
    key(bgui::input_key::z);EXPECT_EQ(files.script_source(root/"enemy.lua"),prior_delete);
}

#include "opengl_test_support.hpp"
#include "os/window.hpp"
#include "systems/ui_system.hpp"
#include <bgui_backend_gl3.hpp>
#include <bgui_backend_glfw.hpp>
TEST_F(SceneWorkflowTest, NativeGraphAndBlockLayoutRendersInsideDock) {
    if(!test_support::supports_opengl33()) GTEST_SKIP() << "OpenGL 3.3 unavailable";
    std::ofstream(root/"a.scene") << R"({"entities":[{"components":{"code":{"directory":"enemy.lua"}}}]})";
    std::ofstream(root/"b.scene") << R"({"entities":[]})";
    std::ofstream(root/"enemy.lua") << "function update()\n  gameObject.transform:Translate(Vector3(1, 2, 3))\n  if Time.time > 10 then\n    SceneManager.LoadScene(\"b.scene\")\n  end\nend\n";
    commons::window window("Scene workflow rendering",1600,1000);
    window.add<commons::ui_system>();
    auto& text_dock=bgui::get_layout().add<bgui::dock>(); text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root); files.build(text_dock);
    auto& scene_dock=bgui::get_layout().add<bgui::dock>();scene_dock.set_enable(false);
    editor::ui::panel_manager manager(dock),scenes(scene_dock);
    auto actions=editor::ui::install_scene_workflow(scenes,manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    auto show=actions.scenes;
    const auto frame=[&]{
        bgui::glfw_update(bgui::get_context());bgui::load_font_queue();bgui::on_update();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1600,1000);glDisable(GL_FRAMEBUFFER_SRGB);
        bgui::gl3_clear();bgui::gl3_render(bgui::get_draw_data());
    };
    frame();frame();
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";}); ASSERT_TRUE(script);
    script->on_pressed();script->on_released();frame();frame();
    auto* input=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Condição";}); ASSERT_TRUE(input);
    EXPECT_GT(input->get_inputbox().processed_width(),80);
    auto* x_field=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="X";});ASSERT_TRUE(x_field);
    auto* z_field=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Z";});ASSERT_TRUE(z_field);
    EXPECT_GT(x_field->get_inputbox().processed_width(),20);
    EXPECT_GT(z_field->processed_x(),x_field->processed_x());
    auto* selected_block=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()==":: Mover";});ASSERT_TRUE(selected_block);
    selected_block->on_pressed();selected_block->on_released();frame();
    if(const auto* capture=std::getenv("BENGINE_WORKFLOW_SCREENSHOT")) {
        std::vector<unsigned char> pixels(1600*1000*3);glReadPixels(0,0,1600,1000,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
        std::ofstream output(capture,std::ios::binary);output<<"P6\n1600 1000\n255\n";
        for(int y=999;y>=0;--y) output.write(reinterpret_cast<const char*>(pixels.data()+y*1600*3),1600*3);
    }
    dock.set_enable(false);scene_dock.set_enable(true);frame();frame();
    auto* fit=find<bgui::button>(scene_dock,[](auto& b){return b.get_label().get_buffer()=="Enquadrar";}); ASSERT_TRUE(fit);
    fit->on_pressed();fit->on_released();frame();frame();
    auto* canvas=find<bgui::element>(scene_dock,[](auto& e){return e.type=="scene-connection-canvas";}); ASSERT_TRUE(canvas);
    EXPECT_GT(canvas->processed_width(),300); EXPECT_GT(canvas->processed_height(),300);
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1600,1000);glDisable(GL_FRAMEBUFFER_SRGB);

    EXPECT_EQ(glGetError(),GL_NO_ERROR);
    auto* a=find<bgui::button>(*canvas,[](auto& b){return b.get_label().get_buffer()=="a.scene";});
    auto* b=find<bgui::button>(*canvas,[](auto& b){return b.get_label().get_buffer()=="b.scene";}); ASSERT_TRUE(a);ASSERT_TRUE(b);
    const int edge_x=(a->processed_x()+a->processed_width()+b->processed_x())/2;
    const int edge_y=a->processed_y()+a->processed_height()/2;
    std::array<unsigned char,3> edge_pixel{};
    glReadPixels(edge_x,999-edge_y,1,1,GL_RGB,GL_UNSIGNED_BYTE,edge_pixel.data());
    EXPECT_GT(edge_pixel[1],100); EXPECT_GT(edge_pixel[2],100);
    bgui::get_context().m_mouse_position={a->processed_x()+10,a->processed_y()+10};
    a->on_pressed(); bgui::get_context().m_mouse_position.x+=45; bgui::get_context().m_mouse_position.y-=30;
    a->set_drag({45,-30}); a->on_released(); frame();frame();
    const auto coordinates=editor::ui::project_files::read(root/".editor-scene-graph.json");
    rapidjson::Document positions;positions.Parse(coordinates.c_str());
    ASSERT_TRUE(positions.HasMember("positions"));
    const auto x=positions["positions"]["a.scene"][0].GetFloat(); EXPECT_GT(x,1);
    auto* graph_window=find<bgui::window>(scene_dock,[](auto& w){return w.get_title().get_buffer()=="Conexões entre cenas";});ASSERT_TRUE(graph_window);
    scene_dock.remove_window(graph_window);show();frame();frame();
    canvas=find<bgui::element>(scene_dock,[](auto& e){return e.type=="scene-connection-canvas";});ASSERT_TRUE(canvas);
    a=find<bgui::button>(*canvas,[](auto& n){return n.get_label().get_buffer()=="a.scene";});ASSERT_TRUE(a);
    EXPECT_NEAR(a->processed_x(),canvas->processed_x()+canvas->processed_width()/2.f+x,1.5);

}


TEST_F(SceneWorkflowTest, SceneOutlinesKeepSeparateRecoverableDraftsAndDetectDiskConflicts) {
    std::ofstream(root/"b.scene") << R"({"entities":[]})";
    auto& text_dock=bgui::get_layout().add<bgui::dock>();
    text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root); files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    bgui::on_update();
    auto* outline=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Roteiro da cena";});
    ASSERT_TRUE(outline);
    const auto choose=[&](std::size_t i) {
        auto* choices=find<bgui::combo_box>(*outline,[](auto&){return true;});
        ASSERT_TRUE(choices);
        choices->set_selected_index(i);
        bgui::on_update();
    };
    const auto click=[&](const std::string& title) {
        auto* b=find<bgui::button>(*outline,[&](auto& v){return v.get_label().get_buffer()==title;});
        ASSERT_TRUE(b); b->on_pressed(); b->on_released(); bgui::on_update();
    };
    choose(1); click("Criar roteiro");
    auto* input=find<bgui::inputbox>(*outline,[](auto& e){return e.type=="scene-development-outline";});
    ASSERT_TRUE(input);
    input->set_buffer("Objetivo: abrir a porta.\n");
    bgui::on_update();
    EXPECT_TRUE(files.has_document_edits(root/"a.scene.roteiro.txt"));
    EXPECT_EQ(editor::ui::project_files::read(root/"a.scene.roteiro.txt"),"");
    choose(2); click("Criar roteiro");
    input->set_buffer("Cena B: encontrar a chave.");
    bgui::on_update(); click("Salvar roteiro");
    EXPECT_EQ(editor::ui::project_files::read(root/"b.scene.roteiro.txt"),"Cena B: encontrar a chave.");
    choose(1);
    EXPECT_EQ(input->get_buffer(),"Objetivo: abrir a porta.\n");
    ASSERT_TRUE(files.save_session(root/"outline-session"));
    EXPECT_NE(editor::ui::project_files::read(root/"outline-session").find("abrir a porta"),std::string::npos);
    std::ofstream(root/"a.scene.roteiro.txt") << "Edição externa";
    click("Salvar roteiro");
    EXPECT_EQ(editor::ui::project_files::read(root/"a.scene.roteiro.txt"),"Edição externa");
    EXPECT_EQ(input->get_buffer(),"Objetivo: abrir a porta.\n");
    dock.remove_window(outline); show(); bgui::on_update();
    outline=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Roteiro da cena";});
    ASSERT_TRUE(outline); choose(1);
    input=find<bgui::inputbox>(*outline,[](auto& e){return e.type=="scene-development-outline";});
    ASSERT_TRUE(input);
    EXPECT_EQ(input->get_buffer(),"Objetivo: abrir a porta.\n");
    editor::ui::files_view recovered(root); recovered.build(text_dock);
    recovered.restore_session(root/"outline-session");
    EXPECT_NE(recovered.text_source(root/"a.scene.roteiro.txt").find("abrir a porta"),std::string::npos);
    EXPECT_THROW(recovered.script_source(root/"a.scene.roteiro.txt"),std::runtime_error);
}

TEST_F(SceneWorkflowTest, SceneOutlineRespectsPlayGuardAndSourceControlHasNoGitSideEffects) {
    auto& text_dock=bgui::get_layout().add<bgui::dock>(); text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root); files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    bool editable=false;
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[&]{return editable;});
    bgui::on_update();
    for(const auto* title:{"Source Control","Diff","Roteiro da cena"})
        ASSERT_TRUE(find<bgui::window>(dock,[&](auto& w){return w.get_title().get_buffer()==title;}));
    auto* choices=find<bgui::combo_box>(dock,[](auto&){return true;}); ASSERT_TRUE(choices);
    choices->set_selected_index(1); bgui::on_update();
    auto* create=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="Criar roteiro";});
    ASSERT_TRUE(create); create->on_pressed(); create->on_released(); bgui::on_update();
    EXPECT_FALSE(std::filesystem::exists(root/"a.scene.roteiro.txt"));
    EXPECT_FALSE(std::filesystem::exists(root/".git"));
    editable=true;
    create->on_pressed(); create->on_released(); bgui::on_update();
    EXPECT_TRUE(std::filesystem::exists(root/"a.scene.roteiro.txt"));
    auto* source=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Source Control";});
    dock.remove_window(source);
    auto* diff=find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Diff";});
    dock.remove_window(diff); show(); bgui::on_update();
    EXPECT_TRUE(find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Source Control";}));
    EXPECT_TRUE(find<bgui::window>(dock,[](auto& w){return w.get_title().get_buffer()=="Diff";}));
}

TEST_F(SceneWorkflowTest, GeneralFppOpensAsNestedBlocksWithoutOpaqueScript) {
    const std::string source(editor::ui::script_template(editor::ui::lua_script_preset::fpp_controller).source);
    std::ofstream(root/"enemy.lua") << source;
    auto& text_dock=bgui::get_layout().add<bgui::dock>(); text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root); files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto show=editor::ui::install_scene_workflow(manager,files,[&]{return root;},[](const auto&){},[]{return true;});
    bgui::on_update();
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";});
    ASSERT_TRUE(script); script->on_pressed(); script->on_released();
    bgui::on_update(); bgui::on_update();
    auto* tree=find<bgui::linear>(dock,[](auto& e){return e.type=="visual-script-tree";});
    ASSERT_TRUE(tree);
    EXPECT_TRUE(find<bgui::element>(*tree,[](auto& e){return e.type=="visual-block-body";}));
    EXPECT_TRUE(find<bgui::button>(*tree,[](auto& b){return b.get_label().get_buffer()==":: A cada quadro";}));
    EXPECT_FALSE(find<bgui::button>(*tree,[](auto& b){return b.get_label().get_buffer()==":: Lua preservado";}));
    EXPECT_TRUE(find<bgui::field>(*tree,[](auto& f){return f.get_label().get_buffer()=="Condição";}));
    EXPECT_EQ(files.script_source(root/"enemy.lua"),source);
    EXPECT_FALSE(files.has_document_edits(root/"enemy.lua"));
}

TEST_F(SceneWorkflowTest, RightClickInBlocksCreatesAppliesAndRemovesScripts) {
    auto context=std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    context->project_root=root;
    const auto object=context->registry->create(); context->registry->entity_names[object.id]="Player";
    context->selected_entity=object.id;
    auto& text_dock=bgui::get_layout().add<bgui::dock>();text_dock.set_enable(false);
    auto& dock=bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(root);files.build(text_dock);
    editor::ui::panel_manager manager(dock);
    auto actions=editor::ui::install_scene_workflow(manager,manager,files,[&]{return root;},[](const auto&){},[]{return true;},[]{},context);
    bgui::on_update();
    auto* script=find<bgui::button>(dock,[](auto& b){return b.get_label().get_buffer()=="enemy.lua";});
    ASSERT_TRUE(script);script->on_pressed();script->on_released();bgui::on_update();bgui::on_update();
    const auto menu=[&] {
        auto* field=find<bgui::field>(dock,[](auto& f){return f.get_label().get_buffer()=="Mensagem";});
        ASSERT_TRUE(field);
        auto& input=field->get_inputbox();
        auto& io=bgui::get_context();
        io.m_mouse_position={input.processed_x()+5,input.processed_y()+5};
        io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::release;bgui::on_update();
        io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::press;bgui::on_update();bgui::on_update();
        io.m_input_map[bgui::input_key::mouse_right]=bgui::input_action::release;bgui::on_update();
        std::cerr << "input " << input.processed_x() << "," << input.processed_y() << " " << input.processed_width() << "x" << input.processed_height() << " target ";
        for(auto* e=bgui::get_mouse_target();e;e=e->get_parent()) std::cerr << e->type << ":" << e->processed_x() << "," << e->processed_y() << " ";
        std::cerr << "\n";
    };
    const auto click=[&](const std::string& label) {
        auto* button=find<bgui::button>(bgui::get_layout(),[&](auto& b){return b.get_label().get_buffer()==label;});
        ASSERT_TRUE(button) << label;button->on_released();bgui::on_update();bgui::on_update();
    };
    menu(); click("Aplicar a objeto...");
    ASSERT_TRUE(find<bgui::combo_box>(bgui::get_layout(),[](auto& b){return b.has_class("script-object-choice");}));
    click("Aplicar script");
    auto attached=context->registry->get<commons::code>(object.id);ASSERT_TRUE(attached);
    EXPECT_EQ(attached->file,"enemy.lua");EXPECT_TRUE(attached->scripts.empty());
    EXPECT_TRUE(context->undo());EXPECT_FALSE(context->registry->has<commons::code>(object.id));
    EXPECT_TRUE(context->redo());
    menu();click("Remover de objeto...");click("Remover associação");
    EXPECT_FALSE(context->registry->has<commons::code>(object.id));EXPECT_TRUE(std::filesystem::exists(root/"enemy.lua"));
    EXPECT_TRUE(context->undo());
    menu();click("Novo script Lua...");
    auto* name=find<bgui::inputbox>(bgui::get_layout(),[](auto& b){return b.has_class("new-script-name");});
    ASSERT_TRUE(name);name->set_buffer("GeneralFpp");click(editor::ui::tr("Criar"));
    EXPECT_TRUE(std::filesystem::is_regular_file(root/"GeneralFpp.lua"));
    EXPECT_NE(files.script_source(root/"GeneralFpp.lua").find("properties.player"),std::string::npos);
    auto* row=find<editor::ui::context_action_button>(dock,[](auto& b){return b.get_label().get_buffer()=="GeneralFpp.lua";});
    ASSERT_TRUE(row);row->open_context();bgui::on_update();bgui::on_update();
    click("Excluir arquivo de script...");
    EXPECT_TRUE(std::filesystem::exists(root/"GeneralFpp.lua"));
    click(editor::ui::tr("Excluir definitivamente"));
    EXPECT_FALSE(std::filesystem::exists(root/"GeneralFpp.lua"));
    EXPECT_TRUE(find<bgui::text>(dock,[](auto& t){return t.get_buffer()=="Selecione um script no painel de scripts";}));
}
