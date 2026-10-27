#include "project_persistence_system.hpp"
#include "project_creation_system.hpp"
#include "project_preview.hpp"

#include "components/camera.hpp"
#include "components/code.hpp"
#include "components/physics.hpp"
#include "components/point_light.hpp"
#include "components/renderer.hpp"
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
#include <vector>

namespace {

std::filesystem::path temporary_project_path() {
    const auto timestamp = std::chrono::high_resolution_clock::now()
        .time_since_epoch()
        .count();
    return std::filesystem::temp_directory_path() /
        ("cpp-bengine-project-" + std::to_string(timestamp) + ".bengine");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };
}

std::filesystem::path scene_file_for_project(
    const std::filesystem::path& project_path
) {
    rapidjson::Document project;
    const auto contents = read_file(project_path);
    project.Parse(contents.data(), contents.size());
    if (project.IsObject() && project.HasMember("scene") &&
        project["scene"].IsString()) {
        const std::filesystem::path scene_path(project["scene"].GetString());
        return scene_path.is_absolute()
            ? scene_path
            : project_path.parent_path() / scene_path;
    }
    return project_path.parent_path() /
        (project_path.stem().string() + ".scene");
}

void remove_project_files(const std::filesystem::path& project_path) {
    std::error_code ignored;
    std::filesystem::remove(scene_file_for_project(project_path), ignored);
    std::filesystem::remove(project_path, ignored);
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
    registry->get<commons::transform>(editor_camera_entity.id)->set_rotation(
        {11.f, 22.f, 33.f}
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
    const auto saved_project_contents = read_file(path);
    rapidjson::Document saved_project;
    saved_project.Parse(saved_project_contents.data(), saved_project_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    EXPECT_FALSE(saved_project.HasMember("entities"));
    EXPECT_TRUE(saved_project.HasMember("scene"));
    const auto scene_path = scene_file_for_project(path);
    const auto saved_scene_contents = read_file(scene_path);
    rapidjson::Document saved_scene;
    saved_scene.Parse(saved_scene_contents.data(), saved_scene_contents.size());
    ASSERT_FALSE(saved_scene.HasParseError());
    EXPECT_STREQ(saved_scene["format"].GetString(), "bubble-engine-scene");
    ASSERT_EQ(saved_scene["entities"].Size(), 1U);
    EXPECT_EQ(
        saved_scene["entities"][0]["id"].GetUint(),
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
    EXPECT_FLOAT_EQ(editor_position.x, 2.f);
    EXPECT_FLOAT_EQ(editor_position.y, 3.f);
    EXPECT_FLOAT_EQ(editor_position.z, 4.f);
    const auto editor_rotation =
        registry->get<commons::transform>(editor_camera_entity.id)->get_rotation();
    EXPECT_FLOAT_EQ(editor_rotation.x, 11.f);
    EXPECT_FLOAT_EQ(editor_rotation.y, 22.f);
    EXPECT_FLOAT_EQ(editor_rotation.z, 33.f);
    const auto scene_position =
        registry->get<commons::transform>(scene_camera_entity.id)->get_position();
    EXPECT_FLOAT_EQ(scene_position.x, 5.f);
    EXPECT_FLOAT_EQ(scene_position.y, 6.f);
    EXPECT_FLOAT_EQ(scene_position.z, 7.f);

    remove_project_files(path);
}

TEST(ProjectPreviewTest, UsesAProjectSpecificPreviewFilePath) {
    const auto project = std::filesystem::path("projects/sample.bengine");
    EXPECT_EQ(
        editor::ui::project_preview_path(project.string()),
        std::filesystem::path("projects/sample.bengine.preview.png")
    );
}

TEST(ProjectCreationTest, CreatesAnEmptyProjectWithStandardFolders) {
    const auto unique_path = temporary_project_path();
    const auto project_folder = unique_path.parent_path() / unique_path.stem();
    std::string error;

    ASSERT_TRUE(editor::ui::create_project_files(project_folder, error)) << error;
    const auto project_file = editor::ui::project_file_path(project_folder);
    EXPECT_TRUE(std::filesystem::is_regular_file(project_file));
    EXPECT_TRUE(std::filesystem::is_regular_file(project_folder / "project.scene"));
    EXPECT_TRUE(std::filesystem::is_directory(project_folder / "assets" / "models"));
    EXPECT_TRUE(std::filesystem::is_directory(project_folder / "assets" / "textures"));
    EXPECT_TRUE(std::filesystem::is_directory(project_folder / "scripts"));

    auto loaded_registry = std::make_shared<commons::ecs>();
    auto editor_camera = loaded_registry->create();
    loaded_registry->add<commons::camera>(editor_camera);
    editor::ui::project_persistence_system persistence(
        loaded_registry,
        editor_camera.id
    );
    ASSERT_TRUE(persistence.load(project_file.string(), error)) << error;
    EXPECT_TRUE(loaded_registry->entities.empty() == false);
    EXPECT_EQ(loaded_registry->entities.size(), 1U);

    std::error_code ignored;
    std::filesystem::remove_all(project_folder, ignored);
}

TEST(ProjectPersistenceTest, KeepsLoadedEntityIdsInTheLiveRegistry) {
    auto source_registry = std::make_shared<commons::ecs>();
    auto source_camera = source_registry->create(17);
    source_registry->add<commons::camera>(source_camera);
    const auto scene_entity = source_registry->create(23);

    editor::ui::project_persistence_system source_persistence(
        source_registry,
        source_camera.id
    );
    const auto path = temporary_project_path();
    std::string error;
    ASSERT_TRUE(source_persistence.save(path.string(), error)) << error;

    auto destination_registry = std::make_shared<commons::ecs>();
    auto destination_camera = destination_registry->create(17);
    destination_registry->add<commons::camera>(destination_camera);
    destination_registry->create(99);
    editor::ui::project_persistence_system destination_persistence(
        destination_registry,
        destination_camera.id
    );

    ASSERT_TRUE(destination_persistence.load(path.string(), error)) << error;
    EXPECT_TRUE(destination_registry->is_alive(scene_entity.id));
    EXPECT_NE(
        destination_registry->get<commons::transform>(scene_entity.id),
        nullptr
    );
    EXPECT_TRUE(destination_registry->validate_invariants());

    remove_project_files(path);
}

TEST(ProjectPersistenceTest, LoadsStandaloneSceneFiles) {
    auto source_registry = std::make_shared<commons::ecs>();
    auto source_camera = source_registry->create(17);
    source_registry->add<commons::camera>(source_camera);
    const auto scene_entity = source_registry->create(23);
    source_registry->get<commons::transform>(scene_entity.id)->set_position(
        {3.f, 4.f, 5.f}
    );

    const auto scene_path = temporary_project_path().replace_extension(".scene");
    editor::ui::project_persistence_system source_persistence(
        source_registry,
        source_camera.id
    );
    std::string error;
    ASSERT_TRUE(source_persistence.save(scene_path.string(), error)) << error;

    auto destination_registry = std::make_shared<commons::ecs>();
    auto destination_camera = destination_registry->create(17);
    destination_registry->add<commons::camera>(destination_camera);
    editor::ui::project_persistence_system destination_persistence(
        destination_registry,
        destination_camera.id
    );
    ASSERT_TRUE(destination_persistence.load(scene_path.string(), error)) << error;
    const auto transform =
        destination_registry->get<commons::transform>(scene_entity.id);
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(transform->get_position().x, 3.f);
    EXPECT_FLOAT_EQ(transform->get_position().y, 4.f);
    EXPECT_FLOAT_EQ(transform->get_position().z, 5.f);

    std::filesystem::remove(scene_path);
}

TEST(ProjectPersistenceTest, LoadsLegacyCombinedProjectFiles) {
    const auto path = temporary_project_path();
    {
        std::ofstream legacy_project(path, std::ios::binary);
        legacy_project << R"({
            "format": "bubble-engine-project",
            "version": 1,
            "settings": {"gravity": [1.0, -4.0, 2.0]},
            "entities": [{
                "id": 23,
                "components": {
                    "transform": {
                        "position": [3.0, 4.0, 5.0],
                        "rotation": [0.0, 0.0, 0.0],
                        "scale": [1.0, 1.0, 1.0]
                    }
                }
            }]
        })";
    }

    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    editor::ui::project_persistence_system persistence(registry, editor_camera.id);
    std::string error;

    ASSERT_TRUE(persistence.load(path.string(), error)) << error;
    const auto transform = registry->get<commons::transform>(23);
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(transform->get_position().x, 3.f);
    EXPECT_FLOAT_EQ(persistence.global_gravity()[1], -4.f);

    remove_project_files(path);
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

    const auto saved_contents = read_file(scene_file_for_project(path));
    rapidjson::Document saved_project;
    saved_project.Parse(saved_contents.data(), saved_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    EXPECT_EQ(saved_project["entities"][0]["name"].GetString(), std::string("Coin"));

    entity_names.clear();
    ASSERT_TRUE(persistence.load(path.string(), error)) << error;
    EXPECT_EQ(entity_names.at(entity.id), std::string("Coin"));

    remove_project_files(path);
}

TEST(ProjectPersistenceTest, SavesRendererMaterialVisibilityAndLockState) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    auto entity = registry->create(23);
    registry->add<commons::renderer>(entity);
    auto renderer = registry->get<commons::renderer>(entity.id);
    ASSERT_NE(renderer, nullptr);
    renderer->m_modelo = new commons::model();
    renderer->visible = false;
    renderer->m_modelo->meshes.emplace_back(
        std::vector<commons::vertex>{},
        std::vector<unsigned int>{},
        commons::material{}
    );
    auto material = renderer->m_modelo->meshes.front().getMaterial();
    material.set_uniform(
        "material.albedo",
        commons::color{0.2f, 0.4f, 0.6f, 0.8f}
    );
    material.set_uniform("material.metallic", 0.7f);
    material.set_uniform("material.roughness", 0.4f);
    material.set_uniform("material.ao", 0.9f);
    material.set_texture("tex_normal", commons::texture{0, "/textures/normal.png"});
    renderer->m_modelo->meshes.front().setMaterial(material);

    editor::ui::project_persistence_system::entity_state_set locked_entities{
        entity.id
    };
    editor::ui::project_persistence_system persistence(
        registry,
        editor_camera.id,
        nullptr,
        nullptr,
        &locked_entities
    );
    const auto path = temporary_project_path();
    std::string error;

    ASSERT_TRUE(persistence.save(path.string(), error)) << error;

    const auto contents = read_file(scene_file_for_project(path));
    rapidjson::Document saved_scene;
    saved_scene.Parse(contents.data(), contents.size());
    ASSERT_FALSE(saved_scene.HasParseError());
    const auto& saved_entity = saved_scene["entities"][0];
    EXPECT_TRUE(saved_entity["locked"].GetBool());
    const auto& saved_renderer =
        saved_entity["components"]["renderer"];
    EXPECT_FALSE(saved_renderer["visible"].GetBool());
    ASSERT_GE(saved_renderer["materials"].Size(), 1U);
    const auto& albedo = saved_renderer["materials"][0]["albedo"];
    const auto& pbr = saved_renderer["materials"][0];
    ASSERT_TRUE(pbr.HasMember("metallic"));
    EXPECT_FLOAT_EQ(pbr["metallic"].GetFloat(), 0.7f);
    EXPECT_FLOAT_EQ(pbr["roughness"].GetFloat(), 0.4f);
    EXPECT_FLOAT_EQ(pbr["ao"].GetFloat(), 0.9f);
    ASSERT_TRUE(pbr.HasMember("maps"));
    EXPECT_STREQ(pbr["maps"]["tex_normal"].GetString(), "/textures/normal.png");
    ASSERT_EQ(albedo.Size(), 4U);
    EXPECT_GE(albedo[0].GetFloat(), 0.f);
    EXPECT_GE(albedo[1].GetFloat(), 0.f);
    EXPECT_GE(albedo[2].GetFloat(), 0.f);
    EXPECT_GE(albedo[3].GetFloat(), 0.f);

    remove_project_files(path);
}

TEST(ProjectPersistenceTest, LoadsRendererVisibilityAndLockState) {
    auto registry = std::make_shared<commons::ecs>();
    auto editor_camera = registry->create(17);
    registry->add<commons::camera>(editor_camera);
    auto entity = registry->create(23);
    registry->add<commons::renderer>(entity);
    auto renderer = registry->get<commons::renderer>(entity.id);
    ASSERT_NE(renderer, nullptr);
    renderer->m_modelo = new commons::model();
    renderer->visible = false;
    renderer->m_modelo->meshes.emplace_back(
        std::vector<commons::vertex>{},
        std::vector<unsigned int>{},
        commons::material{}
    );

    editor::ui::project_persistence_system::entity_state_set hidden_entities{
        entity.id
    };
    editor::ui::project_persistence_system::entity_state_set locked_entities{
        entity.id
    };
    editor::ui::project_persistence_system persistence(
        registry,
        editor_camera.id,
        nullptr,
        &hidden_entities,
        &locked_entities
    );
    const auto path = temporary_project_path();
    std::string error;

    ASSERT_TRUE(persistence.save(path.string(), error)) << error;
    renderer->visible = true;
    hidden_entities.clear();
    locked_entities.clear();

    ASSERT_TRUE(persistence.load(path.string(), error)) << error;

    const auto loaded_renderer =
        registry->get<commons::renderer>(entity.id);
    ASSERT_NE(loaded_renderer, nullptr);
    EXPECT_FALSE(loaded_renderer->visible);
    EXPECT_TRUE(hidden_entities.contains(entity.id));
    EXPECT_TRUE(locked_entities.contains(entity.id));

    remove_project_files(path);
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

    remove_project_files(path);
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

    remove_project_files(path);
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
    const auto saved_contents = read_file(scene_file_for_project(runtime_file));
    rapidjson::Document saved_project;
    saved_project.Parse(saved_contents.data(), saved_contents.size());
    ASSERT_FALSE(saved_project.HasParseError());
    EXPECT_EQ(
        saved_project["entities"][0]["components"]["code"]["directory"]
            .GetString(),
        loaded_code->arquivoCompleto
    );

    remove_project_files(runtime_file);
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
