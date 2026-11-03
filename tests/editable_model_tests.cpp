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

TEST_F(EditableModelTest, IdenticalSavePreservesFileTimestamp) {
    commons::editable_model_data source;
    source.meshes.push_back(quad_mesh());
    const auto path = folder / "unchanged.bmodel";
    std::string error;
    ASSERT_TRUE(commons::save_editable_model(path, source, error)) << error;
    std::filesystem::last_write_time(path,
        std::filesystem::last_write_time(path) - std::chrono::hours(1));
    const auto previous_write = std::filesystem::last_write_time(path);

    ASSERT_TRUE(commons::save_editable_model(path, source, error)) << error;
    EXPECT_EQ(std::filesystem::last_write_time(path), previous_write);

    source.meshes[0].vertices[0].position.x -= 1;
    ASSERT_TRUE(commons::save_editable_model(path, source, error)) << error;
    EXPECT_NE(std::filesystem::last_write_time(path), previous_write);
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

#include "loaders/material_asset.hpp"
#include "loaders/model.hpp"
#include "util/material.hpp"
#include <utils/texture.hpp>
#include <os/asset_manager.hpp>
#include <os/resource_files.hpp>

TEST_F(EditableModelTest, NativeModelLoadsMaterialWithoutGraphicsContext) {
    commons::material material;
    material.set_uniform("material.albedo", commons::color{.2f,.4f,.8f,1.f});
    material.set_uniform("material.roughness", .37f);
    material.set_uniform("enabled", true);
    material.set_uniform("count", 7);
    material.set_uniform("offset", bgui::vec2{2.f,3.f});
    material.set_uniform("direction", bgui::vec3{1.f,2.f,3.f});
    material.set_uniform("matrix", glm::mat4{2.f});
    float pointer_value=.81f;
    material.set_uniform("snapshot", &pointer_value);
    auto image=std::make_shared<bgui::texture>();
    image->m_size={1.f,1.f}; image->m_buffer={255,128,64,255};
    commons::texture texture; texture.embedded=image;
    material.set_texture("tex_albedo", texture);
    std::filesystem::path reference;
    std::string error;
    ASSERT_TRUE(commons::store_material_asset(folder/"materials",material,reference,error)) << error;
    EXPECT_EQ(reference.extension(), ".bmat");
    std::vector<std::filesystem::path> dependencies;
    ASSERT_TRUE(commons::material_asset_dependencies(reference, dependencies, error)) << error;
    ASSERT_EQ(dependencies.size(), 1u);
    EXPECT_EQ(dependencies.front().extension(), ".png");
    std::ifstream material_file(reference);
    const std::string json{std::istreambuf_iterator<char>(material_file), {}};
    EXPECT_EQ(json.find("\"rgba\""), std::string::npos);
    EXPECT_NE(json.find(dependencies.front().filename().string()), std::string::npos);

    std::filesystem::path repeated;
    ASSERT_TRUE(commons::store_material_asset(folder/"materials",material,repeated,error)) << error;
    EXPECT_EQ(reference,repeated);
    auto model_data=commons::editable_model_data{};
    model_data.meshes.push_back(quad_mesh());
    model_data.meshes[0].material_path=reference.lexically_relative(folder).generic_string();
    const auto path=folder/"native.bmodel";
    ASSERT_TRUE(commons::save_editable_model(path,model_data,error)) << error;
    commons::model loaded(path.string());
    ASSERT_EQ(loaded.meshes.size(),1u);
    EXPECT_FALSE(loaded.meshes[0].isLoaded());
    const auto result=loaded.meshes[0].getMaterial();
    EXPECT_FLOAT_EQ(result.getUniforme("material.roughness").valor.f,.37f);
    EXPECT_FLOAT_EQ(result.getUniforme("material.albedo").valor.color_value.b,.8f);
    EXPECT_FLOAT_EQ(result.getUniforme("snapshot").valor.f,.81f);
    EXPECT_TRUE(result.getUniforme("enabled").valor.b);
    EXPECT_EQ(result.getUniforme("count").valor.i,7);
    EXPECT_FLOAT_EQ(result.getUniforme("matrix").valor.mat4[2][2],2.f);
    const auto texture_path = result.getTextura("tex_albedo").path;
    EXPECT_EQ(std::filesystem::path(texture_path), dependencies.front());
    EXPECT_EQ(bgui::decode_texture_memory(bgui::resource_files::read(texture_path), texture_path).m_buffer,
        image->m_buffer);
    material.set_uniform("material.roughness",.9f);
    ASSERT_TRUE(commons::store_material_asset(folder/"materials",material,repeated,error)) << error;
    EXPECT_NE(reference,repeated);
    commons::material original;
    ASSERT_TRUE(commons::load_material_asset(reference,original,error));
    EXPECT_FLOAT_EQ(original.getUniforme("material.roughness").valor.f,.37f);
    std::vector<std::filesystem::path> updated_dependencies;
    ASSERT_TRUE(commons::material_asset_dependencies(repeated, updated_dependencies, error)) << error;
    EXPECT_EQ(updated_dependencies, dependencies);

}

TEST_F(EditableModelTest, LegacyEmbeddedTextureIsExternalizedOnSave) {
    const auto legacy = folder / "legacy.bmat";
    std::ofstream(legacy) <<
        R"({"format":"bengine-material","version":1,"uniforms":{},"textures":{"tex_albedo":{"path":"","filtering":0,"width":1,"height":1,"rgba":"ff8040ff"}}})";
    commons::material material;
    std::string error;
    ASSERT_TRUE(commons::load_material_asset(legacy, material, error)) << error;
    std::filesystem::path saved;
    ASSERT_TRUE(commons::store_material_asset(folder, material, saved, error)) << error;
    std::vector<std::filesystem::path> dependencies;
    ASSERT_TRUE(commons::material_asset_dependencies(saved, dependencies, error)) << error;
    ASSERT_EQ(dependencies.size(), 1u);
    EXPECT_EQ(dependencies.front().extension(), ".png");
    commons::material loaded;
    ASSERT_TRUE(commons::load_material_asset(saved, loaded, error)) << error;
    const auto path = loaded.getTextura("tex_albedo").path;
    EXPECT_EQ(bgui::decode_texture_memory(bgui::resource_files::read(path), path).m_buffer,
        (std::vector<unsigned char>{255, 128, 64, 255}));
}

TEST_F(EditableModelTest, RejectsInvalidMaterialWithoutReplacingExistingState) {
    commons::material output;
    output.set_uniform("preserved",.42f);
    const auto path=folder/"bad.bmat";
    for(const auto* contents : {
        "{invalid",
        R"({"format":"bengine-material","version":2,"uniforms":{},"textures":{}})",
        R"({"format":"bengine-material","version":1,"uniforms":{"v":{"type":4,"value":[1]}},"textures":{}})",
        R"({"format":"bengine-material","version":1,"uniforms":{},"textures":{"t":{"path":"","filtering":0,"width":2,"height":2,"rgba":"00"}}})"
    }) {
        std::ofstream(path) << contents;
        std::string error;
        EXPECT_FALSE(commons::load_material_asset(path,output,error));
        EXPECT_FALSE(error.empty());
        EXPECT_FLOAT_EQ(output.getUniforme("preserved").valor.f,.42f);
    }
}

TEST_F(EditableModelTest, FaceGroupsRoundTripAndFollowTopologyEdits) {
    commons::editable_model_data source;
    source.meshes.push_back(quad_mesh());
    source.meshes[0].face_groups.push_back({"Front", {0}});
    std::string error;
    const auto path = folder / "groups.bmodel";
    ASSERT_TRUE(commons::save_editable_model(path, source, error)) << error;
    commons::editable_model_data loaded;
    ASSERT_TRUE(commons::load_editable_model(path, loaded, error)) << error;
    ASSERT_EQ(loaded.meshes[0].face_groups.size(), 1u);
    EXPECT_EQ(loaded.meshes[0].face_groups[0].name, "Front");
    ASSERT_TRUE(editor::ui::extrude_faces(loaded.meshes[0], {0}, .25f, error)) << error;
    EXPECT_EQ(loaded.meshes[0].face_groups[0].faces, (std::vector<unsigned int>{0}));
    auto mesh = source.meshes[0];
    ASSERT_TRUE(editor::ui::loop_cut(mesh, {0, 1}, error)) << error;
    EXPECT_EQ(mesh.face_groups[0].faces, (std::vector<unsigned int>{0, 1}));
    mesh.face_groups[0].faces = {999};
    EXPECT_FALSE(mesh.validate(error));
}
