#include <gtest/gtest.h>
#include "api/lua.hpp"
#include "components/pathfinder.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "components/camera.hpp"
#include "core/ecs.hpp"
#include "guizmo/collision_debug.hpp"
#include "project_persistence_system.hpp"
#include <fstream>
#include <os/resource_files.hpp>
#include <chrono>

TEST(Pathfinder, DetoursAroundColliderAndProducesDebugLines) {
    auto registry=std::make_shared<commons::ecs>();
    auto enemy=registry->create(); registry->add<commons::pathfinder>(enemy);
    auto wall=registry->create(); registry->add<commons::physics>(wall,true);
    registry->get<commons::transform>(wall.id)->set_position({2,0,0});
    auto body=registry->get<commons::physics>(wall.id);
    body->fit_to_model=false;
    body->configure_collision(commons::physics::shapes::box_shape,{1,2,2},{0,0,0},true,false);
    auto planner=registry->get<commons::pathfinder>(enemy.id);
    ASSERT_TRUE(planner->find_path({4,0,0}));
    auto path=planner->path(); ASSERT_GT(path.size(),2u);
    bool detour=false; for(auto p:path) detour |= std::abs(p.z)>1.f;
    EXPECT_TRUE(detour);
    auto lines=editor::collision_lines(registry);
    EXPECT_GE(lines.size(),path.size()-1);
    EXPECT_FLOAT_EQ(lines.back().color.x,1.f);
    auto floor=registry->create();
    registry->add<commons::physics>(floor,true,commons::physics::shapes::forma_plano);
    EXPECT_TRUE(planner->find_path({4,0,0}));
    EXPECT_FALSE(planner->find_path({2,0,0})); EXPECT_TRUE(planner->path().empty());
    EXPECT_FALSE(planner->find_path({1000,0,0}));
    body->trigger=true;
    ASSERT_TRUE(planner->find_path({4,0,0}));
    for (auto p : planner->path()) EXPECT_FLOAT_EQ(p.z,0);
    body->trigger=false; body->collision_enabled=false;
    EXPECT_TRUE(planner->find_path({4,0,0}));
    EXPECT_FALSE(planner->find_path({0,3,0}));
    EXPECT_FALSE(planner->configure(0,.25f,32));
    EXPECT_FALSE(planner->configure(.5f,.25f,65));
    registry->remove<commons::pathfinder>(enemy.id);
    EXPECT_FALSE(planner->find_path({0,0,1}));
    EXPECT_TRUE(planner->path().empty());
}
TEST(Pathfinder, LuaRoutingAndDetachedReferences) {
    auto registry=std::make_shared<commons::ecs>();
    auto enemy=registry->create(); registry->add<commons::pathfinder>(enemy);
    sol::state lua; lua.open_libraries(sol::lib::base);
    lua.script("bubble = {input = {}}");
    commons::api::setUpApi(lua,registry,enemy.id);
    auto result=lua.safe_script(R"(
        local p=gameObject:GetComponent("Pathfinder")
        assert(p:Configure(0.5,0.25,32))
        assert(p:FindPath(Vector3(2,0,0)))
        local points=p:GetPath()
        assert(#points >= 2 and points[#points].x==2)
        p:Clear()
        assert(#p:GetPath()==0)
        assert(not SceneManager.LoadScene("assets/next.scene"))
        assert(not SceneManager.IsLoading())
    )",sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    registry->scenes=std::make_shared<commons::scene_control>();
    registry->scenes->active="assets/main.scene";
    std::string requested;
    registry->scenes->load=[&](const std::string& path) { requested=path; return true; };
    result=lua.safe_script(R"(
        assert(SceneManager.GetActiveScene()=="assets/main.scene")
        assert(SceneManager.ReloadScene())
        assert(SceneManager.LoadScene("assets/next.scene"))
    )",sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(requested,"assets/next.scene");
}
TEST(SceneManagement, RuntimeSwitchPreservesManifestAndRejectsInvalidScene) {
    auto directory=std::filesystem::temp_directory_path()/
        ("bengine-navigation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct cleanup { std::filesystem::path p; ~cleanup(){ std::filesystem::remove_all(p); } } clean{directory};
    auto registry=std::make_shared<commons::ecs>();
    auto camera=registry->create(17); registry->add<commons::camera>(camera);
    auto enemy=registry->create(23); registry->add<commons::pathfinder>(enemy);
    auto planner=registry->get<commons::pathfinder>(23); ASSERT_TRUE(planner->configure(1,.4f,16));
    editor::ui::project_persistence_system project(registry,17);
    const auto manifest=(directory/"project.bengine").string(); std::string error;
    ASSERT_TRUE(project.save(manifest,error)) << error;
    auto original=bgui::resource_files::text(manifest);
    auto next=directory/"next.scene";
    std::filesystem::copy_file(project.active_scene(),next);
    ASSERT_TRUE(project.load(manifest,error)) << error;
    project.set_migration_writes_enabled(false);
    ASSERT_TRUE(project.load_runtime_scene(manifest,next,error)) << error;
    EXPECT_EQ(bgui::resource_files::text(manifest),original);
    planner=registry->get<commons::pathfinder>(23); ASSERT_TRUE(planner);
    EXPECT_FLOAT_EQ(planner->cell_size,1); EXPECT_FLOAT_EQ(planner->agent_radius,.4f); EXPECT_EQ(planner->search_radius,16);
    auto pose=registry->get<commons::transform>(23);
    { std::ofstream file(directory/"bad.scene"); file << R"({"format":"bubble-engine-scene","version":1,"entities":[{"id":23,"components":{"pathfinder":{"cell_size":0}}}]})"; }
    EXPECT_FALSE(project.load_runtime_scene(manifest,directory/"bad.scene",error));
    EXPECT_EQ(pose,registry->get<commons::transform>(23));
    EXPECT_FALSE(project.load_runtime_scene(manifest,directory/"../outside.scene",error));
    EXPECT_EQ(bgui::resource_files::text(manifest),original);
    auto snapshots=directory/"snapshots";
    std::filesystem::create_directory(snapshots);
    const auto snapshot=(snapshots/"play.bengine").string();
    ASSERT_TRUE(project.save_snapshot(snapshot,error)) << error;
    rapidjson::Document metadata;
    const auto encoded=bgui::resource_files::text(snapshot); metadata.Parse(encoded.c_str());
    ASSERT_TRUE(metadata.HasMember("runtime_source_project"));
    EXPECT_EQ(fs::path(metadata["runtime_source_project"].GetString()),fs::path(manifest));
    editor::ui::project_persistence_system play(registry,17);
    play.set_migration_writes_enabled(false);
    ASSERT_TRUE(play.load(snapshot,error)) << error;
    ASSERT_TRUE(play.load_runtime_scene(manifest,next,error)) << error;
    EXPECT_EQ(bgui::resource_files::text(manifest),original);
    const auto next_contents=bgui::resource_files::text(next);
    std::filesystem::remove(next);
    auto previous=std::move(bgui::resource_files::mounted);
    struct restore_provider { bgui::resource_files::provider p; ~restore_provider(){ bgui::resource_files::mounted=std::move(p); } } restore{std::move(previous)};
    const std::map<std::filesystem::path,std::string> embedded{{std::filesystem::path(manifest),original},{next,next_contents}};
    bgui::resource_files::mounted.exists=[&](const auto& p) { return embedded.contains(p); };
    bgui::resource_files::mounted.read=[&](const auto& p) {
        const auto& text=embedded.at(p); return bgui::resource_files::bytes(text.begin(),text.end());
    };
    ASSERT_TRUE(project.load_runtime_scene(manifest,next,error)) << error;
    EXPECT_EQ(project.active_scene(),next);
}

TEST(Pathfinder, MalformedSettingsDoNotChangeComponent) {
    commons::pathfinder planner;
    rapidjson::Document data;
    data.Parse(R"({"cell_size":2,"agent_radius":-1})");
    EXPECT_FALSE(planner.analyze(data)); EXPECT_FLOAT_EQ(planner.cell_size,.5f);
    data.Parse(R"({"search_radius":1.5})"); EXPECT_FALSE(planner.analyze(data));
    data.Parse(R"({"cell_size":"large"})"); EXPECT_FALSE(planner.analyze(data));
}
