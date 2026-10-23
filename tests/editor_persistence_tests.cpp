#include "project_persistence_system.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/physics.hpp"
#include "components/point_light.hpp"
#include "components/transform.hpp"

#include <gtest/gtest.h>
#include <rapidjson/document.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>

namespace {

std::filesystem::path temporary_project_path() {
    const auto timestamp = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();
    return std::filesystem::temp_directory_path() /
        ("cpp-bengine-project-" + std::to_string(timestamp) + ".bengine");
}

TEST(ProjectPersistenceTest, KeepsEditorCameraOutOfSceneAndRoundTripsSceneCameras) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera_entity = registry->create(17);
    registry->add<commons::camera>(editor_camera_entity);
    auto scene_camera_entity = registry->create(23);
    registry->add<commons::camera>(scene_camera_entity);

    const auto editor_camera = registry->get<commons::camera>(editor_camera_entity.id);
    const auto scene_camera = registry->get<commons::camera>(scene_camera_entity.id);
    registry->get<commons::transform>(editor_camera_entity.id)->set_position(
        {2.f, 3.f, 4.f}
    );
    registry->get<commons::transform>(scene_camera_entity.id)->set_position(
        {5.f, 6.f, 7.f}
    );

    editor::ui::project_persistence_system persistence(
        registry,
        editor_camera_entity.id
    );
    const auto path = temporary_project_path();
    const auto path_string = path.string();
    std::string error;

    ASSERT_TRUE(persistence.save(path_string, error)) << error;
    std::ifstream saved_file(path, std::ios::binary);
    const std::string saved_contents{
        std::istreambuf_iterator<char>(saved_file),
        std::istreambuf_iterator<char>()
    };
    rapidjson::Document saved_project;
    saved_project.Parse(saved_contents.data(), saved_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    ASSERT_EQ(saved_project["entities"].Size(), 1U);
    EXPECT_EQ(
        saved_project["entities"][0]["id"].GetUint(),
        scene_camera_entity.id
    );

    registry->get<commons::transform>(editor_camera_entity.id)->set_position(
        {-1.f, -1.f, -1.f}
    );
    registry->get<commons::transform>(scene_camera_entity.id)->set_position(
        {-2.f, -2.f, -2.f}
    );

    ASSERT_TRUE(persistence.load(path_string, error)) << error;
    EXPECT_EQ(registry->get<commons::camera>(editor_camera_entity.id), editor_camera);
    EXPECT_NE(registry->get<commons::camera>(scene_camera_entity.id), scene_camera);

    const auto editor_position =
        registry->get<commons::transform>(editor_camera_entity.id)->get_position();
    EXPECT_FLOAT_EQ(editor_position.x, -1.f);
    EXPECT_FLOAT_EQ(editor_position.y, -1.f);
    EXPECT_FLOAT_EQ(editor_position.z, -1.f);
    const auto scene_position =
        registry->get<commons::transform>(scene_camera_entity.id)->get_position();
    EXPECT_FLOAT_EQ(scene_position.x, 5.f);
    EXPECT_FLOAT_EQ(scene_position.y, 6.f);
    EXPECT_FLOAT_EQ(scene_position.z, 7.f);

    std::filesystem::remove(path);
}

TEST(ProjectPersistenceTest, SavesAndLoadsEntityNames) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    registry->get<commons::transform>(editor_camera.id)->set_position({0.f, 0.f, 0.f});

    auto entity = registry->create(23);
    registry->add<commons::transform>(entity);
    std::unordered_map<std::uint32_t, std::string> entity_names{{entity.id, "Coin"}};

    editor::ui::project_persistence_system persistence(registry, 17, &entity_names);
    std::string error;
    const auto path = temporary_project_path();

    ASSERT_TRUE(persistence.save(path.string(), error)) << error;

    std::ifstream saved_file(path, std::ios::binary);
    const std::string saved_contents{
        std::istreambuf_iterator<char>(saved_file),
        std::istreambuf_iterator<char>()
    };
    rapidjson::Document saved_project;
    saved_project.Parse(saved_contents.data(), saved_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    EXPECT_EQ(saved_project["entities"][0]["name"].GetString(), std::string("Coin"));

    entity_names.clear();
    ASSERT_TRUE(persistence.load(path.string(), error)) << error;
    EXPECT_EQ(entity_names.at(entity.id), std::string("Coin"));

    std::filesystem::remove(path);
}

TEST(ProjectPersistenceTest, SavesAndLoadsPhysicsBodies) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    auto body_entity = registry->create(23);
    registry->add<commons::physics>(
        body_entity,
        false,
        commons::physics::shapes::forma_esfera
    );
    registry->get<commons::physics>(body_entity.id)->setMass(2.5f);

    editor::ui::project_persistence_system persistence(registry, 17);
    const auto path = temporary_project_path();
    std::string error;

    ASSERT_TRUE(persistence.save(path.string(), error)) << error;
    ASSERT_TRUE(persistence.load(path.string(), error)) << error;

    const auto loaded_physics =
        registry->get<commons::physics>(body_entity.id);
    ASSERT_NE(loaded_physics, nullptr);
    EXPECT_EQ(
        loaded_physics->e_forma,
        commons::physics::shapes::forma_esfera
    );
    EXPECT_FLOAT_EQ(loaded_physics->m_massa, 2.5f);

    std::filesystem::remove(path);
}

TEST(ProjectPersistenceTest, SavesAndLoadsGlobalGravitySettings) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    editor::ui::project_persistence_system persistence(registry, 17);
    persistence.set_global_gravity({1.5f, -4.f, 2.25f});
    const auto path = temporary_project_path();
    std::string error;

    ASSERT_TRUE(persistence.save(path.string(), error)) << error;
    persistence.set_global_gravity({0.f, -9.8f, 0.f});
    ASSERT_TRUE(persistence.load(path.string(), error)) << error;
    const auto& gravity = persistence.global_gravity();
    EXPECT_FLOAT_EQ(gravity[0], 1.5f);
    EXPECT_FLOAT_EQ(gravity[1], -4.f);
    EXPECT_FLOAT_EQ(gravity[2], 2.25f);

    std::filesystem::remove(path);
}

TEST(ProjectPersistenceTest, ResolvesLuaScriptsFromProjectDirectory) {
    const auto directory = temporary_project_path().parent_path() /
        temporary_project_path().stem();
    std::filesystem::create_directories(directory / "scripts");
    {
        std::ofstream script(directory / "scripts" / "logic.lua");
        script << "function setup() end\nfunction update() end\n";
    }

    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    auto scripted_entity = registry->create(23);
    registry->add<commons::code>(
        scripted_entity,
        "scripts/logic.lua"
    );

    editor::ui::project_persistence_system persistence(registry, 17);
    const auto project_file = directory / "project.bengine";
    std::string error;

    ASSERT_TRUE(persistence.save(project_file.string(), error)) << error;
    ASSERT_TRUE(persistence.load(project_file.string(), error)) << error;

    const auto loaded_code = registry->get<commons::code>(scripted_entity.id);
    ASSERT_NE(loaded_code, nullptr);
    EXPECT_EQ(loaded_code->file, "scripts/logic.lua");
    EXPECT_EQ(
        loaded_code->arquivoCompleto,
        std::filesystem::absolute(directory / "scripts" / "logic.lua").string()
    );

    const auto runtime_file = temporary_project_path();
    ASSERT_TRUE(persistence.save(runtime_file.string(), error)) << error;
    std::ifstream saved_runtime_file(runtime_file, std::ios::binary);
    const std::string saved_contents{
        std::istreambuf_iterator<char>(saved_runtime_file),
        std::istreambuf_iterator<char>()
    };
    rapidjson::Document saved_project;
    saved_project.Parse(saved_contents.data(), saved_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    EXPECT_EQ(
        saved_project["entities"][0]["components"]["code"]["directory"]
            .GetString(),
        loaded_code->arquivoCompleto
    );

    std::filesystem::remove(runtime_file);
    std::filesystem::remove_all(directory);
}

TEST(ProjectPersistenceTest, RejectsComponentsWithoutSerializationSupport) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    auto entity = registry->create(23);
    registry->add<commons::point_light>(entity);

    editor::ui::project_persistence_system persistence(registry, 17);
    std::string error;

    EXPECT_FALSE(persistence.save(temporary_project_path().string(), error));
    EXPECT_NE(error.find("does not support project serialization"), std::string::npos);
}

}
