#include "project_persistence_system.hpp"
#include "project_file_transaction.hpp"
#include "components/transform.hpp"
#include "components/camera.hpp"
#include "components/physics.hpp"
#include "components/code.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>

namespace {
namespace files = editor::ui::project_files;
class PersistenceIntegrity : public testing::Test {
protected:
    using Persistence = editor::ui::project_persistence_system;
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("bengine-integrity-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path project = directory / "project.bengine";
    std::filesystem::path scene = directory / "project.scene";
    std::shared_ptr<commons::ecs> registry = std::make_shared<commons::ecs>();
    Persistence::entity_name_map names{{23, "Original"}};
    Persistence::entity_state_set hidden{23}, locked{23};
    Persistence persistence{registry, 17, &names, &hidden, &locked};
    std::string error;
    static void write(const std::filesystem::path& path, const std::string& contents) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.close();
        ASSERT_FALSE(output.fail());
    }
    void SetUp() override {
        std::filesystem::create_directories(directory);
        auto camera = registry->create(17);
        registry->add<commons::camera>(camera);
        auto object = registry->create(23);
        registry->add<commons::physics>(object, true);
        ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    }
    void TearDown() override {
        // Explicit scene shutdown is required by the current shared ownership API.
        const auto ids = registry->live_entities;
        for (const auto id : ids) registry->remove(id);
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    void expect_rejected_without_mutation(const std::filesystem::path& path) {
        const auto entities = registry->entities;
        const auto masks = registry->mascaras;
        const auto live = registry->live_entities;
        const auto old_names = names;
        const auto old_hidden = hidden, old_locked = locked;
        const auto gravity = persistence.global_gravity();
        const auto project_bytes = files::read(project), scene_bytes = files::read(scene);
        EXPECT_FALSE(persistence.load(path.string(), error));
        EXPECT_FALSE(error.empty());
        EXPECT_EQ(registry->entities, entities);
        EXPECT_EQ(registry->mascaras, masks);
        EXPECT_EQ(registry->live_entities, live);
        EXPECT_EQ(names, old_names);
        EXPECT_EQ(hidden, old_hidden);
        EXPECT_EQ(locked, old_locked);
        EXPECT_EQ(persistence.global_gravity(), gravity);
        EXPECT_EQ(files::read(project), project_bytes);
        EXPECT_EQ(files::read(scene), scene_bytes);
    }
};

TEST_F(PersistenceIntegrity, EveryTruncatedScenePrefixIsRejectedWithoutChangingActiveScene) {
    const auto valid = files::read(scene);
    // End before the root closing brace; trailing whitespace alone is valid JSON.
    const auto closing = valid.find_last_of('}');
    ASSERT_NE(closing, std::string::npos);
    const auto broken = directory / "truncated.scene";
    for (std::size_t size = 0; size <= closing; ++size) {
        SCOPED_TRACE(size);
        write(broken, valid.substr(0, size));
        expect_rejected_without_mutation(broken);
    }
}

TEST_F(PersistenceIntegrity, CorruptSceneSchemaAndHierarchyAreRejectedAtomically) {
    const auto valid = files::read(scene);
    const auto broken = directory / "corrupt.scene";
    const std::vector<std::function<void(rapidjson::Document&)>> corruptions{
        [](auto& d) { d["version"].SetInt(999); },
        [](auto& d) { d["entities"].SetObject(); },
        [](auto& d) { d["entities"][0]["id"].SetUint(0); },
        [](auto& d) { d["entities"][0]["id"].SetInt(-1); },
        [](auto& d) { d["entities"][0]["components"]["transform"]["position"].SetArray(); },
        [](auto& d) { d["entities"][0]["components"]["transform"]["scale"][0].SetString("bad"); },
        [](auto& d) { d["entities"][0].AddMember("parent", 23, d.GetAllocator()); },
        [](auto& d) { d["entities"][0].AddMember("parent", 999, d.GetAllocator()); },
        [](auto& d) {
            rapidjson::Value duplicate(d["entities"][0], d.GetAllocator());
            d["entities"].PushBack(duplicate, d.GetAllocator());
        }
    };
    for (std::size_t index = 0; index < corruptions.size(); ++index) {
        SCOPED_TRACE(index);
        rapidjson::Document document;
        document.Parse(valid.data(), valid.size());
        ASSERT_FALSE(document.HasParseError());
        ASSERT_FALSE(document["entities"].Empty());
        corruptions[index](document);
        write(broken, files::encode(document));
        expect_rejected_without_mutation(broken);
    }
}

TEST_F(PersistenceIntegrity, CorruptReferencedSceneLeavesProjectAndLiveComponentsUntouched) {
    write(scene, "{\"format\":\"bubble-engine-scene\",\"entities\":[");
    expect_rejected_without_mutation(project);
}

TEST_F(PersistenceIntegrity, FailedLoadDoesNotRetainAnExtraReferenceToActiveComponents) {
    const auto component = registry->get<commons::transform>(23);
    const auto body = registry->get<commons::physics>(23);
    const auto component_refs = component.use_count(), body_refs = body.use_count();
    const auto broken = directory / "bad.scene";
    write(broken, "{");
    for (unsigned attempt = 0; attempt < 50; ++attempt)
        ASSERT_FALSE(persistence.load(broken.string(), error));
    EXPECT_EQ(component.use_count(), component_refs);
    EXPECT_EQ(body.use_count(), body_refs);
}

TEST_F(PersistenceIntegrity, RepeatedSuccessfulLoadReleasesSupersededTransformLuaAndBulletBody) {
    const auto script = directory / "reload.lua";
    write(script, "function setup() end\n");
    commons::entity object{23};
    registry->add<commons::code>(object, script.string());
    ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    for (unsigned attempt = 0; attempt < 25; ++attempt) {
        std::weak_ptr<commons::transform> old_transform = registry->get<commons::transform>(23);
        std::weak_ptr<commons::physics> old_body = registry->get<commons::physics>(23);
        std::weak_ptr<commons::code> old_script = registry->get<commons::code>(23);
        const auto camera = registry->get<commons::camera>(17);
        ASSERT_TRUE(persistence.load(project.string(), error)) << error;
        EXPECT_TRUE(old_transform.expired());
        EXPECT_TRUE(old_body.expired());
        EXPECT_TRUE(old_script.expired());
        ASSERT_NE(registry->get<commons::code>(23), nullptr);
        EXPECT_EQ(registry->get<commons::camera>(17), camera);
        EXPECT_EQ(registry->live_entities.size(), 2u);
        EXPECT_TRUE(registry->validate_invariants());
    }
}

TEST_F(PersistenceIntegrity, ExplicitSceneShutdownReleasesRegistryLuaAndBulletComponents) {
    std::weak_ptr<commons::ecs> old_registry;
    std::weak_ptr<commons::code> old_script;
    std::weak_ptr<commons::physics> old_body;
    {
        auto local = std::make_shared<commons::ecs>();
        old_registry = local;
        auto object = local->create(1);
        local->add<commons::physics>(object);
        const auto script = directory / "empty.lua";
        write(script, "function setup() end\n");
        local->add<commons::code>(object, script.string());
        old_script = local->get<commons::code>(1);
        old_body = local->get<commons::physics>(1);
        local->remove(1);
        EXPECT_TRUE(old_script.expired());
        EXPECT_TRUE(old_body.expired());
    }
    EXPECT_TRUE(old_registry.expired());
}

TEST_F(PersistenceIntegrity, InvalidSecondJournalEntryCannotPartiallyRestoreFirstFile) {
    rapidjson::Document journal;
    journal.SetObject();
    auto& allocator = journal.GetAllocator();
    journal.AddMember("version", 1, allocator);
    rapidjson::Value entries(rapidjson::kArrayType);
    rapidjson::Value first(rapidjson::kObjectType);
    first.AddMember("path", rapidjson::Value(scene.string().c_str(), allocator), allocator);
    first.AddMember("before", "must never overwrite scene", allocator);
    first.AddMember("after", "replacement", allocator);
    entries.PushBack(first, allocator);
    // The first entry is valid. Reject the complete journal before restoring it.
    entries.PushBack(rapidjson::Value(rapidjson::kObjectType), allocator);
    journal.AddMember("files", entries, allocator);
    const auto path = files::journal_path(project);
    const auto contents = files::encode(journal);
    write(path, contents);
    expect_rejected_without_mutation(project);
    EXPECT_EQ(files::read(path), contents);
}

TEST_F(PersistenceIntegrity, InvalidJournalShapesAreRetainedWithoutTouchingSavedFiles) {
    for (const auto* contents : {"", "null", "[]", "{", R"({"version":2,"files":[]})",
                              R"({"version":1,"files":[]})",
                              R"({"version":1,"files":[{},{},{}]})"}) {
        SCOPED_TRACE(contents);
        const auto path = files::journal_path(project);
        write(path, contents);
        expect_rejected_without_mutation(project);
        EXPECT_EQ(files::read(path), contents);
    }
}

TEST_F(PersistenceIntegrity, FailedSaveCleansOwnedTemporaryFilesAndCanBeRetried) {
    const auto old_project = files::read(project), old_scene = files::read(scene);
    const auto blocker = files::temporary_path(project);
    std::filesystem::create_directory(blocker);
    names[23] = "Changed";
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_EQ(files::read(project), old_project);
    EXPECT_EQ(files::read(scene), old_scene);
    EXPECT_FALSE(std::filesystem::exists(files::temporary_path(scene)));
    EXPECT_FALSE(std::filesystem::exists(files::journal_path(project)));
    EXPECT_TRUE(std::filesystem::is_directory(blocker));
    std::filesystem::remove(blocker);
    ASSERT_TRUE(persistence.save(project.string(), error)) << error;
    names.clear();
    ASSERT_TRUE(persistence.load(project.string(), error)) << error;
    EXPECT_EQ(names.at(23), "Changed");
}

TEST_F(PersistenceIntegrity, HardLinkedSaveDestinationIsRejectedWithoutChangingEitherLink) {
    const auto alias = directory / "alias.scene";
    std::error_code ec;
    std::filesystem::create_hard_link(scene, alias, ec);
    if (ec) GTEST_SKIP() << "Hard links unavailable: " << ec.message();
    const auto old_scene = files::read(scene), old_project = files::read(project);
    names[23] = "Changed";
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(files::read(scene), old_scene);
    EXPECT_EQ(files::read(alias), old_scene);
    EXPECT_EQ(files::read(project), old_project);
}
}

TEST_F(PersistenceIntegrity, ReferencedComponentSurvivesRemovalUntilItsLastOwnerReleasesIt) {
    auto held = registry->get<commons::physics>(23);
    std::weak_ptr<commons::physics> observer = held;
    registry->remove(23);
    EXPECT_FALSE(observer.expired());
    EXPECT_EQ(registry->get<commons::physics>(23), nullptr);
    ASSERT_NE(held->m_rigid_body, nullptr);
    held->setVelocidade({1, 2, 3});
    EXPECT_FLOAT_EQ(held->getVelocidade().y, 2);
    held.reset();
    EXPECT_TRUE(observer.expired());
}

TEST_F(PersistenceIntegrity, SymlinkedTemporaryCannotOverwriteAnUnrelatedFile) {
    const auto unrelated = directory / "unrelated.txt";
    write(unrelated, "preserve me");
    std::error_code ec;
    std::filesystem::create_symlink(unrelated, files::temporary_path(scene), ec);
    if (ec) GTEST_SKIP() << "Symlinks unavailable: " << ec.message();
    const auto old_project = files::read(project), old_scene = files::read(scene);
    EXPECT_FALSE(persistence.save(project.string(), error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(files::read(unrelated), "preserve me");
    EXPECT_EQ(files::read(scene), old_scene);
    EXPECT_EQ(files::read(project), old_project);
    EXPECT_TRUE(std::filesystem::is_symlink(files::temporary_path(scene)));
}

TEST_F(PersistenceIntegrity, EveryTruncatedProjectPrefixIsRejectedWithoutChangingActiveScene) {
    const auto valid = files::read(project);
    const auto closing = valid.find_last_of('}');
    ASSERT_NE(closing, std::string::npos);
    const auto broken = directory / "truncated.bengine";
    for (std::size_t size = 0; size <= closing; ++size) {
        SCOPED_TRACE(size);
        write(broken, valid.substr(0, size));
        expect_rejected_without_mutation(broken);
    }
}

TEST_F(PersistenceIntegrity, EscapedUnicodeNamesAndLocalTransformSurviveRepeatedSaveLoad) {
    const std::string name = "Jogador çã 漢字 \"nome\"\nlinha\tfinal\\";
    names[23] = name;
    const auto transform = registry->get<commons::transform>(23);
    transform->position = {-12.5f, 0.125f, 1000.f};
    transform->rotation = {15.f, -30.f, 45.f};
    transform->scale = {2.f, 3.f, 4.f};
    for (unsigned attempt = 0; attempt < 5; ++attempt) {
        ASSERT_TRUE(persistence.save(project.string(), error)) << error;
        names.clear();
        registry->get<commons::transform>(23)->position = {0, 0, 0};
        ASSERT_TRUE(persistence.load(project.string(), error)) << error;
        EXPECT_EQ(names.at(23), name);
        const auto restored = registry->get<commons::transform>(23);
        EXPECT_FLOAT_EQ(restored->position.x, -12.5f);
        EXPECT_FLOAT_EQ(restored->position.y, 0.125f);
        EXPECT_FLOAT_EQ(restored->position.z, 1000.f);
        EXPECT_FLOAT_EQ(restored->rotation.y, -30.f);
        EXPECT_FLOAT_EQ(restored->scale.z, 4.f);
    }
}
