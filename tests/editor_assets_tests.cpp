#include <gtest/gtest.h>

#include "system/project_assets.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <rapidjson/document.h>

namespace {
    struct temporary_tree {
        std::filesystem::path root{
            std::filesystem::temp_directory_path() /
            ("cpp-bengine-assets-test-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))};

        ~temporary_tree() {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    };

    bool write_text(const std::filesystem::path& path, const std::string& contents) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << contents;
        return output.good();
    }
}

TEST(EditorAssetsTest, PackagesOnlyModelAndReferencedDependencies) {
    temporary_tree tree;
    const auto project_root = tree.root / "Project";
    const auto source_directory = tree.root / "Source";
    const auto scene_path = project_root / "Scenes" / "main.bscene";
    const auto model_path = source_directory / "model.obj";

    ASSERT_TRUE(write_text(model_path,
        "mtllib material.mtl\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "usemtl surface\n"
        "f 1 2 3\n"));
    ASSERT_TRUE(write_text(source_directory / "material.mtl",
        "newmtl surface\n"
        "map_Kd texture.png\n"));
    ASSERT_TRUE(write_text(source_directory / "texture.png", "image placeholder"));
    ASSERT_TRUE(write_text(source_directory / "unrelated.txt", "must not be packaged"));
    ASSERT_TRUE(write_text(scene_path,
        R"({"entities":[{"components":{"renderer":{"model":"../../Source/model.obj"}}}]})"));

    ASSERT_TRUE(editor::project_assets::package_scene_assets(scene_path, project_root));

    std::ifstream input(scene_path, std::ios::binary);
    const std::string contents(std::istreambuf_iterator<char>(input), {});
    rapidjson::Document document;
    document.Parse(contents.c_str());
    ASSERT_FALSE(document.HasParseError());
    const auto& model = document["entities"][0]["components"]["renderer"]["model"];
    ASSERT_TRUE(model.IsString());
    const auto packaged_model = project_root / std::filesystem::path(model.GetString());
    const auto packaged_directory = packaged_model.parent_path();
    EXPECT_TRUE(std::filesystem::is_regular_file(packaged_model));
    EXPECT_TRUE(std::filesystem::is_regular_file(packaged_directory / "material.mtl"));
    EXPECT_TRUE(std::filesystem::is_regular_file(packaged_directory / "texture.png"));
    EXPECT_FALSE(std::filesystem::exists(packaged_directory / "unrelated.txt"));
}

TEST(EditorAssetsTest, FailsWhenAReferencedDependencyIsMissing) {
    temporary_tree tree;
    const auto project_root = tree.root / "Project";
    const auto model_path = tree.root / "Source" / "model.obj";
    const auto scene_path = project_root / "Scenes" / "main.bscene";

    ASSERT_TRUE(write_text(model_path, "mtllib missing.mtl\n"));
    ASSERT_TRUE(write_text(scene_path,
        R"({"entities":[{"components":{"renderer":{"model":"../../Source/model.obj"}}}]})"));

    EXPECT_FALSE(editor::project_assets::package_scene_assets(scene_path, project_root));
}
