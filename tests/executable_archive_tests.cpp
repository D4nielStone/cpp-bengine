#include <gtest/gtest.h>
#include "tools/game_packager/runtime_template.hpp"
#include "apps/runtime/game_bootstrap.hpp"
#include <os/asset_manager.hpp>
#include <utils/theme.hpp>
#include <resources/assimp_resource_io.hpp>
#include <loaders/model_import.hpp>
#include <components/code.hpp>
#include <core/ecs.hpp>
#include <components/camera.hpp>
#include "project_persistence_system.hpp"
#include <chrono>

namespace {
namespace fs=std::filesystem;
void write(const fs::path& path,const std::string& text){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text;}
class ExecutableArchiveTest:public testing::Test {
protected:
    fs::path root=fs::temp_directory_path()/("bengine packed "+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path data=root/"source", output=root/"moved/Runner";
    void SetUp() override {
        write(root/"runtime",std::string("stub executable BENGINE_EMBED_REQUIRED_V1:0"));
        write(data/"game.json",R"({"format":"bubble-engine-game","version":1,"project":"game/project.bengine","executable":"Game","binary":"Game"})");
        write(data/"game/project.bengine",R"({"format":"bubble-engine-project","version":1,"scene":"main.scene"})");
        write(data/"game/main.scene",R"({"format":"bubble-engine-scene","version":1,"entities":[]})");
        write(data/"game/scripts/main.lua","print('packed')");
        write(data/"empty","");
    }
    void TearDown() override {bgui::resource_files::mounted={};fs::remove_all(root);}
    void pack(){executable_archive::append(root/"runtime",data,"Game",output);}
};
}
TEST_F(ExecutableArchiveTest, ReadsCompressedFilesAndEmptyFilesWithoutCreatingLooseFiles){
    write(data/"large",std::string(100000,'a'));pack();
    auto archive=executable_archive::reader::open(output);ASSERT_NE(archive,nullptr);
    EXPECT_EQ(archive->read("large"),executable_archive::bytes(100000,'a'));
    EXPECT_TRUE(archive->read("empty").empty());
    EXPECT_LT(fs::file_size(output),2000u);
    fs::remove_all(data);
    ASSERT_TRUE(game_bootstrap::mount_executable(output));
    auto launch=game_bootstrap::resolve(output.parent_path());
    EXPECT_EQ(launch.project,(output.parent_path()/"game/project.bengine").lexically_normal());
    EXPECT_FALSE(fs::exists(launch.project));
    EXPECT_FALSE(game_bootstrap::diagnostics_enabled(output.parent_path()));
    EXPECT_EQ(bgui::resource_files::text("game/scripts/main.lua"),"print('packed')");
}
TEST_F(ExecutableArchiveTest, LooseFilesCannotOverrideEmbeddedResources){
    pack();fs::remove_all(data);write(output.parent_path()/"game/scripts/main.lua","modified");
    ASSERT_TRUE(game_bootstrap::mount_executable(output));
    EXPECT_EQ(bgui::resource_files::text(output.parent_path()/"game/scripts/main.lua"),"print('packed')");
    EXPECT_FALSE(bgui::resource_files::exists("../../etc/passwd"));
    EXPECT_THROW(bgui::resource_files::read("../../etc/passwd"),std::runtime_error);
}
TEST_F(ExecutableArchiveTest, RejectsCorruptionAndTruncatedIndex){
    pack();auto archive=executable_archive::reader::open(output);ASSERT_NE(archive,nullptr);
    {std::fstream file(output,std::ios::binary|std::ios::in|std::ios::out);file.seekp(-1,std::ios::end);file.put('X');}
    EXPECT_THROW(executable_archive::reader::open(output),std::runtime_error);
    pack();fs::resize_file(output,fs::file_size(output)-8);
    EXPECT_EQ(executable_archive::reader::open(output),nullptr);
    EXPECT_FALSE(executable_archive::safe_name("../outside"));
    EXPECT_FALSE(executable_archive::safe_name("C:/outside"));
}
TEST_F(ExecutableArchiveTest, ImportsModelAndSidecarDirectlyFromExecutable){
    write(data/"game/assets/mesh.obj","mtllib mesh.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl color\nf 1 2 3\n");
    write(data/"game/assets/mesh.mtl","newmtl color\nKd 1 0 0\n");
    write(data/"game/assets/mesh.obj.import.json",R"({"version":1,"scale":2})");
    pack();fs::remove_all(data);ASSERT_TRUE(game_bootstrap::mount_executable(output));
    Assimp::Importer importer;
    importer.SetIOHandler(new commons::resource_io());
    const auto* scene=importer.ReadFile((output.parent_path()/"game/assets/mesh.obj").string(),aiProcess_Triangulate);
    ASSERT_NE(scene,nullptr) << importer.GetErrorString();
    ASSERT_TRUE(scene->HasMeshes());
    EXPECT_DOUBLE_EQ(commons::read_model_import_options(output.parent_path()/"game/assets/mesh.obj").scale,2);
    EXPECT_FALSE(fs::exists(output.parent_path()/"game/assets"));
}
TEST_F(ExecutableArchiveTest, DirectoryExportsAlsoDisableDiagnosticsAndDeveloperRuntimeKeepsThem){
    EXPECT_TRUE(game_bootstrap::diagnostics_enabled(root));
    write(root/"game.json",R"({"profiling":true})");
    EXPECT_FALSE(game_bootstrap::diagnostics_enabled(root));
}
TEST_F(ExecutableArchiveTest, ValidatesPrecompiledTemplateCompatibility){
    const auto folder=root/"template";
    game_package::create_template(root/"runtime",folder,"engine-key","linux","single");
    EXPECT_NO_THROW(game_package::validate_template(folder,"engine-key","linux","single"));
    EXPECT_THROW(game_package::validate_template(folder,"other-key","linux","single"),std::runtime_error);
    EXPECT_THROW(game_package::validate_template(folder,"engine-key","windows","single"),std::runtime_error);
    EXPECT_THROW(game_package::validate_template(folder,"engine-key","linux","folder"),std::runtime_error);
}

TEST_F(ExecutableArchiveTest, DecodesImageFromExecutableWithoutAFile) {
    const unsigned char png[] = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137,0,0,0,13,73,68,65,84,120,156,99,248,207,192,240,31,0,5,0,1,255,137,153,61,29,0,0,0,0,73,69,78,68,174,66,96,130};
    write(data/"game/assets/red.png",std::string(reinterpret_cast<const char*>(png),sizeof(png)));
    pack();fs::remove_all(data);ASSERT_TRUE(game_bootstrap::mount_executable(output));
    const auto& texture=bgui::asset_manager::get_instance().load_texture((output.parent_path()/"game/assets/red.png").string());
    EXPECT_FLOAT_EQ(texture.m_size.x,1);EXPECT_FLOAT_EQ(texture.m_size.y,1);
    EXPECT_EQ(texture.m_buffer,(std::vector<unsigned char>{255,0,0,255}));
    EXPECT_FALSE(fs::exists(output.parent_path()/"game/assets/red.png"));
}
TEST_F(ExecutableArchiveTest, SinglePublicationHasOnlyExecutableAndCanReplaceAnOlderSingleExport) {
#ifdef _WIN32
    const auto executable = "Game.exe";
#else
    const auto executable = "Game";
#endif
    write(data/"game.json", std::string(R"({"format":"bubble-engine-game","version":1,"project":"game/project.bengine","executable":"Game","binary":")") + executable + "\"}");
    write(data/executable,"stub executable BENGINE_EMBED_REQUIRED_V1:0");
    const auto destination=root/"exports/linux";
    game_package::publish(data,destination,true);
    ASSERT_EQ(std::distance(fs::directory_iterator(destination),fs::directory_iterator{}),1);
    EXPECT_TRUE(executable_archive::reader::open(destination/executable)->contains("game/scripts/main.lua"));
    EXPECT_NO_THROW(game_package::publish(data,destination,true));
    write(data/"lib/unexpected.so","library");
    EXPECT_THROW(game_package::publish(data,destination,true),std::runtime_error);
    EXPECT_TRUE(fs::is_regular_file(destination/executable));
}

TEST_F(ExecutableArchiveTest, LuaLoadfileAndDofileReadEmbeddedScripts) {
    write(data/"game/scripts/main.lua", "result=assert(loadfile('game/scripts/helper.lua'))(); dofile('game/scripts/second.lua')");
    write(data/"game/scripts/helper.lua", "return 42");
    write(data/"game/scripts/second.lua", "second=99");
    pack();fs::remove_all(data);ASSERT_TRUE(game_bootstrap::mount_executable(output));
    auto registry=std::make_shared<commons::ecs>();auto entity=registry->create();
    registry->add<commons::code>(entity,(output.parent_path()/"game/scripts/main.lua").string());
    auto script=registry->get<commons::code>(entity.id);script->setup();
    EXPECT_EQ(script->estado_lua["result"].get<int>(),42);
    EXPECT_EQ(script->estado_lua["second"].get<int>(),99);
    auto result=script->estado_lua.safe_script("local f,e=loadfile('../../outside.lua'); assert(f==nil and e~=nil)");
    EXPECT_TRUE(result.valid());
}

TEST_F(ExecutableArchiveTest, ProjectLoaderReadsEmbeddedSceneAndCannotSaveIntoGameResources) {
    pack();fs::remove_all(data);ASSERT_TRUE(game_bootstrap::mount_executable(output));
    auto registry=std::make_shared<commons::ecs>();auto camera=registry->create(77);
    registry->add<commons::camera>(camera);
    editor::ui::project_persistence_system project(registry,camera.id);
    const auto entry=output.parent_path()/"game/project.bengine";
    std::string error;
    ASSERT_TRUE(project.load(entry.string(),error)) << error;
    EXPECT_FALSE(project.save(entry.string(),error));
    EXPECT_NE(error.find("read-only"),std::string::npos);
    EXPECT_FALSE(fs::exists(entry));
}

TEST_F(ExecutableArchiveTest, LoadsThemeDirectlyFromExecutable) {
    write(data/"assets/themes/custom.theme", "[base]\nvisual.visible = false\n");
    pack();fs::remove_all(data);ASSERT_TRUE(game_bootstrap::mount_executable(output));
    EXPECT_TRUE(bgui::resource_files::exists(output.parent_path()/"assets/themes/custom.theme"));
    const auto theme=bgui::load_theme((output.parent_path()/"assets/themes/custom.theme").string());
    ASSERT_TRUE(theme.base.visual.visible.has_value());
    EXPECT_FALSE(*theme.base.visual.visible);
    EXPECT_FALSE(fs::exists(output.parent_path()/"assets/themes/custom.theme"));
}

#include "loaders/editable_model.hpp"
#include "loaders/material_asset.hpp"
#include "loaders/model.hpp"

TEST_F(ExecutableArchiveTest, RuntimeLoadsNativeModelAndMaterialFromPackedResources) {
    commons::material material;
    material.set_uniform("material.roughness",.26f);
    material.set_uniform("material.albedo",commons::color{.1f,.3f,.7f,1.f});
    fs::path reference;std::string error;
    ASSERT_TRUE(commons::store_material_asset(data/"game/assets/materials",material,reference,error)) << error;
    commons::editable_mesh_data mesh;
    mesh.vertices={{{0,0,0},{0,0,1},{}},{{1,0,0},{0,0,1},{}},{{0,1,0},{0,0,1},{}}};
    mesh.faces={{0,1,2}};
    mesh.material_path=reference.lexically_relative(data/"game/assets").generic_string();
    commons::editable_model_data source;source.meshes.push_back(mesh);
    ASSERT_TRUE(commons::save_editable_model(data/"game/assets/native.bmodel",source,error)) << error;
    pack();fs::remove_all(data);
    ASSERT_TRUE(game_bootstrap::mount_executable(output));
    commons::model model((output.parent_path()/"game/assets/native.bmodel").string());
    ASSERT_EQ(model.meshes.size(),1u);
    EXPECT_FLOAT_EQ(model.meshes[0].getMaterial().getUniforme("material.roughness").valor.f,.26f);
    EXPECT_EQ(model.meshes[0].indices().size(),3u);
    EXPECT_FALSE(fs::exists(output.parent_path()/"game/assets/native.bmodel"));
}
