#include "project_migration_manager.hpp"
#include "release_check.hpp"
#include <gtest/gtest.h>
#include <chrono>

using editor::ui::project_migration_manager;
TEST(SemanticVersion, StableOrderingAndStrictSyntax) {
    using commons::semantic_version;
    EXPECT_LT(*semantic_version::parse("0.9.9"), *semantic_version::parse("0.10.0"));
    EXPECT_LT(*semantic_version::parse("1.0.99"), *semantic_version::parse("2.0.0"));
    for (const auto* text : {"01.0.0", "1.2", "1.2.3.4", "1.2.3junk", "-1.0.0", "1.2.4294967296", "1.2.3-beta"})
        EXPECT_FALSE(semantic_version::parse(text)) << text;
}
TEST(ReleaseCheck, OnlyNewStableReleasesAreUpdates) {
    EXPECT_EQ(editor::ui::release_status(R"({"tag_name":"v99.0.0","draft":false,"prerelease":false})"), "Update available: v99.0.0");
    EXPECT_EQ(editor::ui::release_status(R"({"tag_name":"v0.0.1","draft":false,"prerelease":false})"), "Engine is up to date");
    for (const auto* text : {"bad", "[]", R"({"tag_name":2})", R"({"tag_name":"v99.0.0","draft":true,"prerelease":false})", R"({"tag_name":"v99.0.0-rc1","draft":false,"prerelease":false})"})
        EXPECT_EQ(editor::ui::release_status(text), "Update check unavailable");
}
TEST(ProjectMigration, IncrementalAndIdempotentPreservesUnknownFields) {
    rapidjson::Document doc;
    doc.Parse(R"({"version":1,"custom":{"keep":true}})");
    bool changed = false;
    std::string error;
    ASSERT_TRUE(project_migration_manager::convert(doc, changed, error)) << error;
    EXPECT_TRUE(changed);
    EXPECT_EQ(doc["version"].GetUint(), 2u);
    EXPECT_TRUE(doc["custom"]["keep"].GetBool());
    EXPECT_STREQ(doc["engine_version"].GetString(), BUBBLE_ENGINE_VERSION);
    EXPECT_STREQ(doc["project_version"].GetString(), "0.1.0");
    EXPECT_TRUE(project_migration_manager::convert(doc, changed, error));
    EXPECT_FALSE(changed);
}
TEST(ProjectMigration, RejectsFutureOrMalformedManifest) {
    for (const auto* text : {"null", "[]", R"({"version":3})", R"({"version":0})", R"({"version":2,"engine_version":"99.0.0","minimum_engine_version":"0.1.0","project_version":"1.0.0"})", R"({"version":2,"engine_version":"0.1.0","minimum_engine_version":"99.0.0","project_version":"1.0.0"})", R"({"version":1,"project_version":"1.0.0"})"}) {
        rapidjson::Document doc;
        doc.Parse(text);
        std::string error;
        bool changed = false;
        EXPECT_FALSE(project_migration_manager::convert(doc, changed, error));
        EXPECT_FALSE(error.empty());
    }
}
TEST(ProjectMigration, VerifiedBackupAndTransactionalConversion) {
    namespace fs = std::filesystem;
    auto root = fs::temp_directory_path() / ("bengine-migrate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto file = root / "project.bengine";
    const std::string before = R"({"version":1,"custom":true})";
    { std::ofstream output(file); output << before; }
    rapidjson::Document doc;
    doc.Parse(before.c_str());
    std::string error;
    bool changed = false;
    ASSERT_TRUE(project_migration_manager::convert(doc, changed, error));
    ASSERT_TRUE(project_migration_manager::commit(file, doc, error)) << error;
    EXPECT_EQ(editor::ui::project_files::read(fs::path(file.string() + ".migration-backup-0") / file.filename()), before);
    EXPECT_EQ(editor::ui::project_files::read(file), editor::ui::project_files::encode(doc));
    EXPECT_FALSE(fs::exists(editor::ui::project_files::journal_path(file)));
    fs::remove_all(root);
}

TEST(ProjectMigration, ConcurrentEditIsPreservedBeforeBackupOrReplacement) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("bengine-migration-edit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto file = root / "project.bengine";
    const std::string original = R"({"version":1})";
    const std::string edited = R"({"version":1,"edited":true})";
    { std::ofstream output(file); output << edited; }
    rapidjson::Document doc;
    doc.Parse(original.c_str());
    bool changed = false;
    std::string error;
    ASSERT_TRUE(project_migration_manager::convert(doc, changed, error));
    EXPECT_FALSE(project_migration_manager::commit(file, doc, error, &original));
    EXPECT_EQ(editor::ui::project_files::read(file), edited);
    EXPECT_FALSE(fs::exists(file.string() + ".migration-backup-0"));
    fs::remove_all(root);
}
