#include <gtest/gtest.h>

#include "mesh_topology_edit.hpp"
#include "loaders/editable_model.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

commons::editable_mesh_data quad_mesh() {
    commons::editable_mesh_data mesh;
    mesh.vertices = {
        {{0.f, 0.f, 0.f}, {}, {0.f, 0.f}},
        {{1.f, 0.f, 0.f}, {}, {1.f, 0.f}},
        {{1.f, 1.f, 0.f}, {}, {1.f, 1.f}},
        {{0.f, 1.f, 0.f}, {}, {0.f, 1.f}}
    };
    mesh.faces = {{0, 1, 2, 3}};
    mesh.recalculate_normals();
    return mesh;
}

class EditableModelTest : public testing::Test {
protected:
    std::filesystem::path folder =
        std::filesystem::temp_directory_path() /
        ("bengine-editable-model-" +
         std::to_string(std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count()));

    void SetUp() override {
        std::filesystem::create_directories(folder);
    }

    void TearDown() override {
        std::error_code error;
        std::filesystem::remove_all(folder, error);
    }
};

}

TEST_F(EditableModelTest, ExtrudesSelectedPolygonAndCreatesSideFaces) {
    auto mesh = quad_mesh();
    std::string error;

    ASSERT_TRUE(editor::ui::extrude_faces(mesh, {0}, 1.f, error)) << error;
    EXPECT_EQ(mesh.vertices.size(), 8u);
    ASSERT_EQ(mesh.faces.size(), 5u);
    EXPECT_FLOAT_EQ(mesh.vertices[4].position.z, 1.f);
    EXPECT_EQ(mesh.triangulated_indices().size(), 30u);
}

TEST_F(EditableModelTest, LoopCutSplitsQuadAndRejectsUnsupportedTopology) {
    auto mesh = quad_mesh();
    std::string error;

    ASSERT_TRUE(
        editor::ui::loop_cut(mesh, editor::ui::ordered_mesh_edge(0, 1), error)
    ) << error;
    EXPECT_EQ(mesh.vertices.size(), 6u);
    ASSERT_EQ(mesh.faces.size(), 2u);
    EXPECT_EQ(mesh.faces[0].size(), 4u);
    EXPECT_EQ(mesh.faces[1].size(), 4u);

    auto triangle = mesh;
    triangle.faces = {{0, 1, 2}};
    EXPECT_FALSE(editor::ui::loop_cut(
        triangle,
        editor::ui::ordered_mesh_edge(0, 1),
        error
    ));
    EXPECT_NE(error.find("quadriláteras"), std::string::npos);
}

TEST_F(EditableModelTest, NativeModelRoundTripsAndPreservesPolygonTopology) {
    commons::editable_model_data source;
    source.meshes.push_back(quad_mesh());
    const auto path = folder / "model.bmodel";
    std::string error;

    ASSERT_TRUE(commons::save_editable_model(path, source, error)) << error;
    commons::editable_model_data loaded;
    ASSERT_TRUE(commons::load_editable_model(path, loaded, error)) << error;
    ASSERT_EQ(loaded.meshes.size(), 1u);
    ASSERT_EQ(loaded.meshes[0].vertices.size(), 4u);
    ASSERT_EQ(loaded.meshes[0].faces.size(), 1u);
    EXPECT_EQ(loaded.meshes[0].faces[0], (std::vector<unsigned int>{0, 1, 2, 3}));
    EXPECT_EQ(loaded.meshes[0].triangulated_indices(),
        (std::vector<unsigned int>{0, 1, 2, 0, 2, 3}));
}

TEST_F(EditableModelTest, RejectsInvalidNativeModelWithoutChangingOutput) {
    const auto path = folder / "invalid.bmodel";
    std::ofstream(path) << R"({"format":"bengine-model","version":1,"meshes":[{"vertices":[[0,0,0,0,0]],"faces":[[0,1,9]]}]})";
    commons::editable_model_data output;
    output.meshes.push_back(quad_mesh());
    std::string error;

    EXPECT_FALSE(commons::load_editable_model(path, output, error));
    ASSERT_EQ(output.meshes.size(), 1u);
    EXPECT_EQ(output.meshes[0].faces[0].size(), 4u);
    EXPECT_FALSE(error.empty());
}
