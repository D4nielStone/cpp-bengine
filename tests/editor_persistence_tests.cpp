#include "project_persistence_system.hpp"

#include "components/camera.hpp"
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
