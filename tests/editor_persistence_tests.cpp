#include "project_persistence_system.hpp"

#include "components/camera.hpp"
#include "components/point_light.hpp"
#include "components/transform.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
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

TEST(ProjectPersistenceTest, RoundTripsEntitiesAndKeepsEditorCameraInstance) {
    auto registry = std::make_shared<commons::ecs>();
    auto camera_entity = registry->create(17);
    registry->add<commons::camera>(camera_entity);

    const auto camera = registry->get<commons::camera>(camera_entity.id);
    registry->get<commons::transform>(camera_entity.id)->set_position(
        {2.f, 3.f, 4.f}
    );

    editor::ui::project_persistence_system persistence(
        registry,
        camera_entity.id
    );
    const auto path = temporary_project_path();
    const auto path_string = path.string();
    std::string error;

    ASSERT_TRUE(persistence.save(path_string, error)) << error;
    registry->get<commons::transform>(camera_entity.id)->set_position(
        {-1.f, -1.f, -1.f}
    );

    ASSERT_TRUE(persistence.load(path_string, error)) << error;
    EXPECT_EQ(registry->get<commons::camera>(camera_entity.id), camera);

    const auto position =
        registry->get<commons::transform>(camera_entity.id)->get_position();
    EXPECT_FLOAT_EQ(position.x, 2.f);
    EXPECT_FLOAT_EQ(position.y, 3.f);
    EXPECT_FLOAT_EQ(position.z, 4.f);

    std::filesystem::remove(path);
}

TEST(ProjectPersistenceTest, RejectsComponentsWithoutSerializationSupport) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create(23);
    registry->add<commons::point_light>(entity);

    editor::ui::project_persistence_system persistence(registry, 23);
    std::string error;

    EXPECT_FALSE(persistence.save(temporary_project_path().string(), error));
    EXPECT_NE(error.find("does not support project serialization"), std::string::npos);
}

}
