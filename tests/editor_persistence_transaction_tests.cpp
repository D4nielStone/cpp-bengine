#include "project_persistence_system.hpp"
#include "project_file_transaction.hpp"
#include <cstdlib>
#include "components/camera.hpp"
#include "components/point_light.hpp"
#include "components/transform.hpp"
#include "components/code.hpp"
#include "systems/code_system.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <iterator>

namespace {
// Windows death tests re-execute fixture setup instead of inheriting filesystem
// state via fork. Keep the real process-interruption checks explicit on POSIX.
#ifdef _WIN32
#define REQUIRE_FORK() GTEST_SKIP() << "Process interruption tests require POSIX fork."
#else
#define REQUIRE_FORK() ::testing::FLAGS_gtest_death_test_style = "fast"
#endif

class PersistenceTransactionTest : public testing::Test {
protected:
    using Persistence = editor::ui::project_persistence_system;
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("bengine-transactions-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path project = directory / "project.bengine";
    std::filesystem::path scene = directory / "project.scene";
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    Persistence::entity_name_map names{{23, "Original"}};
    Persistence::entity_state_set hidden{23}, locked{23};
    Persistence persistence{registry, 17, &names, &hidden, &locked};
    std::string error;

    void SetUp() override {
        std::filesystem::create_directory(directory);
        auto camera = registry->create(17);
        registry->add<commons::camera>(camera);
        registry->create(23);
        ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    }
    void TearDown() override {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    static std::string read(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), {}};
    }
};

TEST_F(PersistenceTransactionTest, FailedSceneTemporaryOpenPreservesSavedFiles) {
    const auto old_project = read(project), old_scene = read(scene);
    names[23] = "Changed";
    std::filesystem::create_directory(scene.string() + ".tmp");
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
}

TEST_F(PersistenceTransactionTest, SerializationFailurePreservesSavedFiles) {
    const auto old_project = read(project), old_scene = read(scene);
    auto entity = registry->create(24);
    registry->add<commons::point_light>(entity);
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_NE(error.find("does not support project serialization"), std::string::npos);
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
}

TEST_F(PersistenceTransactionTest, FailedProjectTemporaryOpenPreservesSavedScene) {
    const auto old_project = read(project), old_scene = read(scene);
    names[23] = "Changed";
    std::filesystem::create_directory(project.string() + ".tmp");
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
}

TEST_F(PersistenceTransactionTest, FailedStagingPreservesNamesAndActiveRegistry) {
    const auto entities = registry->entities;
    const auto masks = registry->mascaras;
    const auto live = registry->live_entities;
    const auto old_names = names;
    const auto old_hidden = hidden, old_locked = locked;
    const auto gravity = persistence.global_gravity();
    const auto camera = registry->get<commons::camera>(17);
    const auto transform = registry->get<commons::transform>(17);
    const auto invalid = directory / "invalid.scene";
    // The first entity is staged before the second is rejected for parenting
    // to the editor camera. Schema validation and hierarchy validation succeed.
    std::ofstream(invalid) << R"({"format":"bubble-engine-scene","version":1,"entities":[
      {"id":24,"name":"Replacement","locked":true,"components":{"transform":{"position":[1,2,3],"rotation":[0,0,0],"scale":[1,1,1]}}},
      {"id":25,"parent":17,"components":{"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}}},
      {"id":17,"components":{"camera":{},"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}}}
    ]})";
    EXPECT_FALSE(persistence.load(invalid.string(), error));
    EXPECT_EQ(error, "Scene entities cannot use the editor camera as a parent.");
    EXPECT_EQ(names, old_names);
    EXPECT_EQ(hidden, old_hidden);
    EXPECT_EQ(locked, old_locked);
    EXPECT_EQ(registry->entities, entities);
    EXPECT_EQ(registry->mascaras, masks);
    EXPECT_EQ(registry->live_entities, live);
    EXPECT_EQ(persistence.global_gravity(), gravity);
    EXPECT_EQ(registry->get<commons::camera>(17), camera);
    EXPECT_EQ(registry->get<commons::transform>(17), transform);
    EXPECT_EQ(camera->reg, registry);
    EXPECT_EQ(transform->reg, registry);
}
TEST_F(PersistenceTransactionTest, InvalidInputsPreserveNamesAndActiveRegistry) {
    const auto entities = registry->entities;
    const auto masks = registry->mascaras;
    const auto live = registry->live_entities;
    const auto old_names = names;
    const auto old_hidden = hidden, old_locked = locked;
    const auto invalid = directory / "invalid.bengine";
    for (const auto* contents : {
        "{",
        R"({"format":"bubble-engine-project","version":1,"scene":"absent.scene"})",
        R"({"format":"bubble-engine-scene","version":1,"entities":[{"id":24,"components":{}}]})"
    }) {
        SCOPED_TRACE(contents);
        std::ofstream(invalid) << contents;
        EXPECT_FALSE(persistence.load(invalid.string(), error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(names, old_names);
        EXPECT_EQ(hidden, old_hidden);
        EXPECT_EQ(locked, old_locked);
        EXPECT_EQ(registry->entities, entities);
        EXPECT_EQ(registry->mascaras, masks);
        EXPECT_EQ(registry->live_entities, live);
    }
}

TEST_F(PersistenceTransactionTest, SuccessfulLoadReplacesSceneAndRebindsComponents) {
    const auto camera = registry->get<commons::camera>(17);
    const auto transform = registry->get<commons::transform>(17);
    registry->create(99);
    names[99] = "Transient";
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_FALSE(registry->live_entities.contains(99));
    EXPECT_FALSE(registry->entities.contains(99));
    EXPECT_FALSE(registry->mascaras.contains(99));
    EXPECT_EQ(names, (Persistence::entity_name_map{{23, "Original"}}));
    EXPECT_EQ(registry->get<commons::camera>(17), camera);
    EXPECT_EQ(registry->get<commons::transform>(17), transform);
    ASSERT_NE(registry->get<commons::transform>(23), nullptr);
    for (const auto& [id, components] : registry->entities) {
        EXPECT_TRUE(registry->live_entities.contains(id));
        EXPECT_TRUE(registry->mascaras.contains(id));
        for (const auto& [mask, component] : components) {
            EXPECT_EQ(component->reg, registry);
            EXPECT_EQ(component->my_object, id);
        }
    }
}

TEST_F(PersistenceTransactionTest, ProcessTerminationRecoversTheCompletePreviousSave) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto old_project = read(project), old_scene = read(scene);
    auto new_scene = old_scene;
    new_scene.replace(new_scene.find("Original"), 8, "Changed");
    const std::vector<files::document> documents{{scene, new_scene}, {project, old_project + "\n"}};
    for (const auto& boundary : std::vector<std::pair<std::string, std::size_t>>{
        {"staged", 0}, {"staged", 1}, {"journaled", 0}, {"replaced", 0}, {"replaced", 1}}) {
        SCOPED_TRACE(boundary.first + std::to_string(boundary.second));
        EXPECT_EXIT({
            files::save(project, documents, error, [&](const char* phase, std::size_t index) {
                if (phase == boundary.first && index == boundary.second) std::_Exit(71);
            });
            std::_Exit(72);
        }, testing::ExitedWithCode(71), "");
        Persistence reopened(registry, 17, &names, &hidden, &locked);
        ASSERT_TRUE(reopened.load(project.string(), error)) << error;
        EXPECT_EQ(read(project), old_project);
        EXPECT_EQ(read(scene), old_scene);
        EXPECT_EQ(names.at(23), "Original");
        EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
    }
}

TEST_F(PersistenceTransactionTest, TerminationAfterCommitKeepsTheNewSave) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    auto new_scene = read(scene);
    new_scene.replace(new_scene.find("Original"), 8, "Changed");
    const std::vector<files::document> documents{{scene, new_scene}, {project, read(project) + "\n"}};
    EXPECT_EXIT({
        files::save(project, documents, error, [](const char* phase, std::size_t) {
            if (std::string(phase) == "committed") std::_Exit(71);
        });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_EQ(names.at(23), "Changed");
    EXPECT_EQ(read(scene), new_scene);
}

TEST_F(PersistenceTransactionTest, RecoveryCanItselfBeInterruptedAndRepeated) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto old_project = read(project), old_scene = read(scene);
    const std::vector<files::document> documents{{scene, "replacement"}, {project, old_project + "\n"}};
    EXPECT_EXIT({
        files::save(project, documents, error, [](const char* phase, std::size_t) {
            if (std::string(phase) == "replaced") std::_Exit(71);
        });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    EXPECT_EXIT({
        files::recover(project, error, [](const char*, std::size_t) { std::_Exit(71); });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
}

TEST_F(PersistenceTransactionTest, ReplacementFailureRollsBackBothFiles) {
    namespace files = editor::ui::project_files;
    const auto old_project = read(project), old_scene = read(scene);
    EXPECT_FALSE(files::save(project, {{scene, "replacement"}, {project, old_project + "\n"}}, error,
        [&](const char* phase, std::size_t index) {
            if (std::string(phase) == "replaced" && index == 0)
                std::filesystem::remove(files::temporary_path(project));
        }));
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
}

TEST_F(PersistenceTransactionTest, FailedRecoveryRetainsJournalForRetry) {
    namespace files = editor::ui::project_files;
    const auto old_project = read(project), old_scene = read(scene);
    EXPECT_FALSE(files::save(project, {{scene, "replacement"}, {project, old_project + "\n"}}, error,
        [&](const char* phase, std::size_t index) {
            if (std::string(phase) == "replaced" && index == 0) {
                std::filesystem::create_directory(files::temporary_path(scene));
                std::filesystem::remove(files::temporary_path(project));
            }
        }));
    EXPECT_TRUE(std::filesystem::exists(files::journal_path(project)));
    EXPECT_FALSE(persistence.load(project.string(), error));
    EXPECT_EQ(names.at(23), "Original");
    std::filesystem::remove(files::temporary_path(scene));
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_EQ(read(scene), old_scene);
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
}

TEST_F(PersistenceTransactionTest, InvalidJournalDoesNotModifyFilesOrRegistry) {
    namespace files = editor::ui::project_files;
    const auto old_project = read(project), old_scene = read(scene);
    const auto entities = registry->entities;
    std::ofstream(files::journal_path(project)) << "{truncated";
    EXPECT_FALSE(persistence.load(project.string(), error));
    EXPECT_NE(error.find("recovery failed"), std::string::npos);
    EXPECT_EQ(read(project), old_project);
    EXPECT_EQ(read(scene), old_scene);
    EXPECT_EQ(registry->entities, entities);
    EXPECT_EQ(names.at(23), "Original");
    EXPECT_TRUE(std::filesystem::exists(files::journal_path(project)));
}

TEST_F(PersistenceTransactionTest, InterruptedFirstSaveRemovesNewFilesOnRecovery) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto new_project = directory / "new.bengine", new_scene = directory / "new.scene";
    const std::vector<files::document> documents{{new_scene, read(scene)},
        {new_project, R"({"scene":"new.scene"})"}};
    EXPECT_EXIT({
        files::save(new_project, documents, error, [](const char* phase, std::size_t) {
            if (std::string(phase) == "replaced") std::_Exit(71);
        });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    ASSERT_TRUE(files::recover(new_project, error)) << error;
    EXPECT_FALSE(std::filesystem::exists(new_scene));
    EXPECT_FALSE(std::filesystem::exists(new_project));
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(new_project)));
}

TEST_F(PersistenceTransactionTest, InterruptedStandaloneSceneSaveIsRecoveredOnLoad) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto old_scene = read(scene);
    EXPECT_EXIT({
        files::save(scene, {{scene, "replacement"}}, error, [](const char* phase, std::size_t) {
            if (std::string(phase) == "replaced") std::_Exit(71);
        });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    ASSERT_TRUE(persistence.load(scene.string(), error)) << error;
    EXPECT_EQ(read(scene), old_scene);
    EXPECT_EQ(names.at(23), "Original");
}

TEST_F(PersistenceTransactionTest, SaveRecoversPendingTransactionBeforeStartingAnother) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto old_project = read(project);
    EXPECT_EXIT({
        files::save(project, {{scene, "replacement"}, {project, old_project}}, error,
            [](const char* phase, std::size_t) {
                if (std::string(phase) == "replaced") std::_Exit(71);
            });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    names[23] = "Latest";
    ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_EQ(names.at(23), "Latest");
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
}

TEST_F(PersistenceTransactionTest, TransactionRejectsOverlappingReservedPaths) {
    namespace files = editor::ui::project_files;
    const auto old_project = read(project);
    for (const auto& collision : {project, files::temporary_path(project),
         files::journal_path(project), files::temporary_path(files::journal_path(project))}) {
        EXPECT_FALSE(files::save(project, {{collision, "scene"}, {project, old_project}}, error));
        EXPECT_EQ(read(project), old_project);
    }
}

TEST_F(PersistenceTransactionTest, JournalCannotRestoreAnUnrelatedFile) {
    REQUIRE_FORK();
    namespace files = editor::ui::project_files;
    const auto unrelated = directory / "unrelated.txt";
    std::ofstream(unrelated) << "Keep me";
    const auto old_project = read(project);
    EXPECT_EXIT({
        files::save(project, {{scene, "replacement"}, {project, old_project}}, error,
            [](const char* phase, std::size_t) {
                if (std::string(phase) == "journaled") std::_Exit(71);
            });
        std::_Exit(72);
    }, testing::ExitedWithCode(71), "");
    rapidjson::Document journal;
    const auto contents = read(files::journal_path(project));
    journal.Parse(contents.c_str());
    journal["files"][0]["path"].SetString(unrelated.string().c_str(), journal.GetAllocator());
    std::ofstream(files::journal_path(project)) << files::encode(journal);
    EXPECT_FALSE(persistence.load(project.string(), error));
    EXPECT_EQ(read(unrelated), "Keep me");
    EXPECT_EQ(read(project), old_project);
    EXPECT_TRUE(std::filesystem::exists(files::journal_path(project)));
}

}

TEST_F(PersistenceTransactionTest, RuntimeLoadsAndResavesNamesWithoutEditorContext) {
    auto runtime_registry = std::make_shared<commons::ecs>();
    auto camera_entity = runtime_registry->create(17);
    runtime_registry->add<commons::camera>(camera_entity);
    Persistence runtime{runtime_registry, 17};
    ASSERT_TRUE(runtime.load(project.string(), error)) << error;
    ASSERT_EQ(runtime_registry->find_by_name("Original"), std::optional<std::uint32_t>{23});
    EXPECT_FALSE(runtime_registry->find_by_name("Missing"));
    const auto script_path = directory / "lookup.lua";
    std::ofstream(script_path) << R"(function setup()
        local object = GameObject.Find('Original')
        assert(object ~= nil and object.id == 23)
        object.transform.localPosition = Vector3(7, 8, 9)
        found_at_startup = true
    end)";
    auto script_entity = runtime_registry->create(24);
    runtime_registry->add<commons::code>(script_entity, script_path.string());
    commons::code_system scripts;
    scripts.setup(runtime_registry);
    EXPECT_TRUE(runtime_registry->get<commons::code>(24)->estado_lua["found_at_startup"].get_or(false));
    EXPECT_FLOAT_EQ(runtime_registry->get<commons::transform>(23)->get_position().x, 7.f);
    const auto invalid = directory / "invalid-names.scene";
    std::ofstream(invalid) << R"({"format":"bubble-engine-scene","version":1,"entities":[
      {"id":24,"name":"Replacement","parent":17,"components":{"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}}}
    ]})";
    EXPECT_FALSE(runtime.load(invalid.string(), error));
    EXPECT_EQ(runtime_registry->find_by_name("Original"), std::optional<std::uint32_t>{23});
    EXPECT_FALSE(runtime_registry->find_by_name("Replacement"));
    ASSERT_TRUE(runtime.save(project.string(), error)) << error;
    EXPECT_NE(read(scene).find("Original"), std::string::npos);
    EXPECT_TRUE(runtime_registry->validate_invariants());
}
