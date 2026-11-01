#include <gtest/gtest.h>
#include "tools/game_packager/game_package.hpp"
#include "apps/runtime/game_bootstrap.hpp"

namespace {
namespace fs = std::filesystem;
void put(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path()); std::ofstream(path) << text;
}
class GameExportTest : public testing::Test {
protected:
    fs::path root = fs::temp_directory_path() / ("bengine export test " +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path repo = root / "engine", project = root / "My Game";
    fs::path staging = root / "cache/package", output = root / "output/linux";
    void SetUp() override {
        put(repo / "commons/assets/models/cube.obj", "engine primitive");
        put(repo / "external/cpp-bgui/assets/fonts/3270NerdFont-Regular.ttf", "gui font");
        put(repo / "external/cpp-bgui/assets/themes/dark.theme", "gui theme");
        put(project / "project.bengine", R"({"format":"bubble-engine-project","version":1,"scene":"main.scene"})");
        scene(R"({"renderer":{"model":"assets/model.obj"},"code":{"directory":"scripts/play.lua"}})");
        put(project / "assets/model.obj", "mtllib model.mtl\n");
        put(project / "assets/model.mtl", "map_Kd texture.png\n");
        put(project / "assets/texture.png", "texture");
        put(project / "assets/model.obj.import.json", R"({"version":1})");
        put(project / "scripts/play.lua", "function setup() end");
    }
    void TearDown() override { fs::remove_all(root); }
    void scene(const std::string& components) {
        put(project / "main.scene", "{\"format\":\"bubble-engine-scene\",\"version\":1,\"entities\":[{\"id\":2,\"components\":" + components + "}]}");
    }
    void prepare() {
        game_package::prepare(repo, project / "project.bengine", staging, output);
        put(root / "compiler/runtime_app", "test executable");
        game_package::stage_binary(root / "compiler/runtime_app", staging, false);
    }
};
}

TEST_F(GameExportTest, CopiesProjectResourcesAndEngineAssetsWithoutBuildOrEditorFiles) {
    put(project / "build/CMakeCache.txt", "cache");
    put(project / ".git/config", "git");
    put(project / "editor-ui.cfg", "layout");
    put(project / "project.bengine.preview.png", "preview");
    prepare();
    for (const auto* path : {"game/project.bengine", "game/main.scene", "game/scripts/play.lua",
            "game/assets/model.obj", "game/assets/model.mtl", "game/assets/texture.png",
            "game/assets/model.obj.import.json", "assets/models/cube.obj", "assets/fonts/3270NerdFont-Regular.ttf",
            "assets/themes/dark.theme", "game.json"})
        EXPECT_TRUE(fs::is_regular_file(staging / path)) << path;
    EXPECT_FALSE(fs::exists(staging / "game/build"));
    EXPECT_FALSE(fs::exists(staging / "game/.git"));
    EXPECT_FALSE(fs::exists(staging / "game/editor-ui.cfg"));
    EXPECT_FALSE(fs::exists(staging / "game/project.bengine.preview.png"));
}

TEST_F(GameExportTest, RelocatesAbsoluteAssetPathsButPreservesOrdinaryTextProperties) {
    rapidjson::Document data;
    data.SetObject(); auto& a = data.GetAllocator();
    data.AddMember("model", rapidjson::Value((repo / "commons/assets/models/cube.obj").string().c_str(), a), a);
    data.AddMember("directory", rapidjson::Value((project / "scripts/play.lua").string().c_str(), a), a);
    rapidjson::Value text(rapidjson::kObjectType);
    text.AddMember("type", "string", a);
    text.AddMember("value", rapidjson::Value((project / "assets/texture.png").string().c_str(), a), a);
    data.AddMember("text", text, a);
    rapidjson::Value image(rapidjson::kObjectType);
    image.AddMember("type", "image", a);
    image.AddMember("value", rapidjson::Value((project / "assets/texture.png").string().c_str(), a), a);
    data.AddMember("image", image, a);
    game_package::preparation paths(project, repo / "commons/assets");
    paths.rewrite(data, a);
    EXPECT_STREQ(data["model"].GetString(), "../assets/models/cube.obj");
    EXPECT_STREQ(data["directory"].GetString(), "scripts/play.lua");
    EXPECT_STREQ(data["image"]["value"].GetString(), "assets/texture.png");
    EXPECT_EQ(std::string(data["text"]["value"].GetString()), (project / "assets/texture.png").string());
}

TEST_F(GameExportTest, ExternalAndMissingAssetsFailWithoutReplacingPreviousGame) {
    prepare(); put(staging / "old.marker", "previous"); game_package::publish(staging, output);
    put(root / "external.lua", "external");
    rapidjson::Document data;
    data.SetObject(); auto& a = data.GetAllocator();
    data.AddMember("directory", rapidjson::Value((root / "external.lua").string().c_str(), a), a);
    game_package::write_json(project / "main.scene", data);
    EXPECT_THROW(prepare(), std::runtime_error);
    EXPECT_TRUE(fs::exists(output / "old.marker"));
    scene(R"({"code":{"directory":"missing.lua"}})");
    EXPECT_THROW(prepare(), std::runtime_error);
    EXPECT_TRUE(fs::exists(output / "old.marker"));
}

TEST_F(GameExportTest, PublicationRemovesStaleGameFilesAndRefusesUnrelatedDirectories) {
    prepare(); game_package::publish(staging, output);
    put(output / "old.file", "obsolete"); game_package::publish(staging, output);
    EXPECT_FALSE(fs::exists(output / "old.file"));
    put(root / "unrelated/personal.txt", "keep");
    EXPECT_THROW(game_package::publish(staging, root / "unrelated"), std::runtime_error);
    EXPECT_TRUE(fs::exists(root / "unrelated/personal.txt"));
    EXPECT_THROW(game_package::stage_binary(root / "missing.exe", staging, true), fs::filesystem_error);
    EXPECT_TRUE(fs::exists(output / "game.json"));
}

TEST_F(GameExportTest, RejectsOutputAncestorsAndDoesNotCopyOutputInsideProject) {
    EXPECT_THROW(game_package::prepare(repo, project / "project.bengine", staging, project), std::runtime_error);
    EXPECT_THROW(game_package::prepare(repo, project / "project.bengine", root, output), std::runtime_error);
    output = project / "exports/linux";
    prepare(); game_package::publish(staging, output);
    prepare();
    EXPECT_FALSE(fs::exists(staging / "game/exports/linux"));
}

TEST_F(GameExportTest, StagesOnlyExecutableAndDllDependenciesWithGameName) {
    prepare();
    put(root / "compiler/runtime_app.exe", "executable");
    put(root / "compiler/assimp.dll", "library");
    put(root / "compiler/runtime_app.pdb", "debug");
    put(root / "compiler/CMakeCache.txt", "cache");
    game_package::stage_binary(root / "compiler/runtime_app.exe", staging, true);
    EXPECT_TRUE(fs::exists(staging / "My_Game.exe"));
    EXPECT_TRUE(fs::exists(staging / "assimp.dll"));
    EXPECT_FALSE(fs::exists(staging / "runtime_app.exe"));
    EXPECT_FALSE(fs::exists(staging / "runtime_app.pdb"));
    EXPECT_FALSE(fs::exists(staging / "CMakeCache.txt"));
}

TEST_F(GameExportTest, BootstrapResolvesPackagedProjectIndependentlyOfWorkingDirectory) {
    prepare(); game_package::publish(staging, output);
    const auto launch = game_bootstrap::resolve(output);
    EXPECT_EQ(launch.directory, fs::canonical(output));
    EXPECT_EQ(launch.project, fs::canonical(output / "game/project.bengine"));
    // Relocate the package and hide its source project.
    fs::rename(output, root / "relocated"); fs::rename(project, root / "hidden-source");
    const auto moved = game_bootstrap::resolve(root / "relocated");
    EXPECT_TRUE(fs::exists(moved.project));
    auto scene = game_package::read_json(moved.directory / "game/main.scene");
    const auto* model = scene["entities"][0]["components"]["renderer"]["model"].GetString();
    EXPECT_TRUE(fs::exists(moved.project.parent_path() / model));
}

TEST_F(GameExportTest, BootstrapRejectsInvalidMissingOrEscapingEntryProject) {
    prepare();
    auto manifest = game_package::read_json(staging / "game.json");
    manifest["project"].SetString("../../outside.bengine", manifest.GetAllocator());
    game_package::write_json(staging / "game.json", manifest);
    EXPECT_THROW(game_bootstrap::resolve(staging), std::runtime_error);
    put(staging / "game.json", "{");
    EXPECT_THROW(game_bootstrap::resolve(staging), std::runtime_error);
    prepare(); fs::remove(staging / "game/project.bengine");
    EXPECT_THROW(game_bootstrap::resolve(staging), std::runtime_error);
}

TEST_F(GameExportTest, ValidatesModelSidecarsAndGltfUrisBeforePublication) {
    put(project / "assets/mesh.bin", "buffer");
    put(project / "assets/scene.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"mesh.bin"}],"images":[{"uri":"texture.png"}]})");
    prepare();
    EXPECT_TRUE(fs::exists(staging / "game/assets/mesh.bin"));
    put(root / "external.png", "external");
    put(project / "assets/model.mtl", "map_Kd ../../external.png\n");
    EXPECT_THROW(prepare(), std::runtime_error);
    put(project / "assets/model.mtl", "map_Kd texture.png\n");
    put(project / "assets/scene.gltf", R"({"images":[{"uri":"../../external.png"}]})");
    EXPECT_THROW(prepare(), std::runtime_error);
}

TEST_F(GameExportTest, ResolvesBuiltinModelFallbackUsedByScenesFromAnotherCheckout) {
    rapidjson::Document data;
    data.Parse(R"({"model":"/old/checkout/commons/assets/models/cube.obj"})");
    game_package::preparation paths(project, repo / "commons/assets");
    paths.rewrite(data, data.GetAllocator());
    EXPECT_STREQ(data["model"].GetString(), "../assets/models/cube.obj");
}

TEST_F(GameExportTest, NormalizesWindowsMaterialPathsAndPercentEncodedGltfUris) {
    put(project / "assets/Texture/two words.png", "texture");
    put(project / "assets/model.mtl", "map_Kd Texture\\two words.png\n");
    put(project / "assets/scene.gltf", R"({"images":[{"uri":"Texture/two%20words.png"}]})");
    prepare();
    auto gltf = game_package::read_json(staging / "game/assets/scene.gltf");
    EXPECT_STREQ(gltf["images"][0]["uri"].GetString(), "Texture/two%20words.png");
    std::ifstream material(staging / "game/assets/model.mtl");
    const std::string contents{std::istreambuf_iterator<char>(material), {}};
    EXPECT_NE(contents.find("Texture/two words.png"), std::string::npos);
}

TEST_F(GameExportTest, IncompletePackageCannotReplacePreviousGame) {
    prepare(); game_package::publish(staging, output);
    put(output / "previous.marker", "keep");
    game_package::prepare(repo, project / "project.bengine", staging, output);
    EXPECT_THROW(game_package::publish(staging, output), std::runtime_error);
    EXPECT_TRUE(fs::exists(output / "previous.marker"));
}

TEST_F(GameExportTest, ExistingBuildDirectoryIsNeverClearedOrMixedIntoGameOutput) {
    put(output.parent_path() / "CMakeCache.txt", "keep");
    EXPECT_THROW(prepare(), std::runtime_error);
    EXPECT_TRUE(fs::exists(output.parent_path() / "CMakeCache.txt"));
    EXPECT_FALSE(fs::exists(output));
}

TEST_F(GameExportTest, CachePreparationCannotDeleteEngineCheckout) {
    EXPECT_THROW(game_package::prepare(repo, project / "project.bengine", repo, output), std::runtime_error);
    EXPECT_TRUE(fs::exists(repo / "commons/assets/models/cube.obj"));
}

TEST_F(GameExportTest, SelectedProjectIsRewrittenEvenWithCustomJsonExtension) {
    auto descriptor = game_package::read_json(project / "project.bengine");
    const auto scene_path = (project / "main.scene").string();
    descriptor["scene"].SetString(scene_path.c_str(), descriptor.GetAllocator());
    game_package::write_json(project / "custom.json", descriptor);
    game_package::prepare(repo, project / "custom.json", staging, output);
    auto copied = game_package::read_json(staging / "game/custom.json");
    EXPECT_STREQ(copied["scene"].GetString(), "main.scene");
    EXPECT_EQ(game_bootstrap::resolve(staging).project, fs::canonical(staging / "game/custom.json"));
    auto scene = game_package::read_json(project / "main.scene");
    const auto builtin = (repo / "commons/assets/models/cube.obj").string();
    scene["entities"][0]["components"]["renderer"]["model"].SetString(builtin.c_str(), scene.GetAllocator());
    game_package::write_json(project / "MAIN.SCENE", scene);
    descriptor["scene"].SetString("MAIN.SCENE", descriptor.GetAllocator());
    game_package::write_json(project / "custom.json", descriptor);
    game_package::prepare(repo, project / "custom.json", staging, output);
    auto upper = game_package::read_json(staging / "game/MAIN.SCENE");
    EXPECT_STREQ(upper["entities"][0]["components"]["renderer"]["model"].GetString(), "../assets/models/cube.obj");
}

TEST_F(GameExportTest, CopiesSingleCrossCubemapReferencedByCamera) {
    put(project / "assets/sky-cross.png", "cross pixels");
    scene(R"({"camera":{"skybox":true,"skybox_image":"assets/sky-cross.png"}})");
    prepare();
    EXPECT_TRUE(fs::is_regular_file(staging / "game/assets/sky-cross.png"));
    const auto document = game_package::read_json(staging / "game/main.scene");
    EXPECT_STREQ(document["entities"][0]["components"]["camera"]["skybox_image"].GetString(), "assets/sky-cross.png");
}
