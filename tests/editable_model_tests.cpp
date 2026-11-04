#include <gtest/gtest.h>

#include "components/physics.hpp"
#include "mesh_topology_edit.hpp"
#include "model_mesh_extract.hpp"
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

TEST_F(EditableModelTest, LoopCutSlidesCoherentlyAcrossOppositeEdgeWinding) {
    auto mesh = quad_mesh();
    mesh.vertices.push_back({{0.f, 2.f, 0.f}, {}, {0.f, 2.f}});
    mesh.vertices.push_back({{1.f, 2.f, 0.f}, {}, {1.f, 2.f}});
    mesh.faces.push_back({3, 2, 5, 4});
    std::string error;
    std::vector<editor::ui::mesh_edge> rails;
    ASSERT_TRUE(editor::ui::loop_cut(mesh, {0, 1}, error, &rails)) << error;
    ASSERT_EQ(rails.size(), 3u);
    editor::ui::slide_loop_cut(mesh, 6, rails, .8f);
    for (std::size_t i = 6; i < mesh.vertices.size(); ++i) {
        EXPECT_FLOAT_EQ(mesh.vertices[i].position.x, .8f);
        EXPECT_FLOAT_EQ(mesh.vertices[i].uvcoords.x, .8f);
    }
    editor::ui::slide_loop_cut(mesh, 6, rails, .5f);
    for (std::size_t i = 6; i < mesh.vertices.size(); ++i)
        EXPECT_FLOAT_EQ(mesh.vertices[i].position.x, .5f);
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

TEST_F(EditableModelTest, DeletesFacesEdgesAndVerticesAndRemapsFaceGroups) {
    auto mesh = quad_mesh();
    mesh.faces = {{0, 1, 2}, {0, 2, 3}};
    mesh.face_groups = {{"Both", {0, 1}}, {"First", {0}}};
    std::string error;

    const auto check_remaining_face = [&](const commons::editable_mesh_data& result) {
        ASSERT_EQ(result.vertices.size(), 3u);
        EXPECT_EQ(result.vertices[1].position, mesh.vertices[2].position);
        EXPECT_EQ(result.faces, (std::vector<std::vector<unsigned int>>{{0, 1, 2}}));
        ASSERT_EQ(result.face_groups.size(), 1u);
        EXPECT_EQ(result.face_groups[0].name, "Both");
        EXPECT_EQ(result.face_groups[0].faces, (std::vector<unsigned int>{0}));
    };
    auto by_face = mesh;
    ASSERT_TRUE(editor::ui::delete_mesh_elements(by_face, {0}, {}, {}, error)) << error;
    check_remaining_face(by_face);
    auto by_vertex = mesh;
    ASSERT_TRUE(editor::ui::delete_mesh_elements(by_vertex, {}, {1}, {}, error)) << error;
    check_remaining_face(by_vertex);
    auto by_edge = mesh;
    ASSERT_TRUE(editor::ui::delete_mesh_elements(by_edge, {}, {}, {{0, 1}}, error)) << error;
    check_remaining_face(by_edge);

    ASSERT_TRUE(editor::ui::delete_mesh_elements(mesh, {}, {}, {{0, 2}}, error)) << error;
    EXPECT_TRUE(mesh.vertices.empty());
    EXPECT_TRUE(mesh.faces.empty());
    EXPECT_TRUE(mesh.face_groups.empty());
    const auto path = folder / "empty-mesh.bmodel";
    ASSERT_TRUE(commons::save_editable_model(path, {{mesh}}, error)) << error;
    commons::editable_model_data loaded;
    ASSERT_TRUE(commons::load_editable_model(path, loaded, error)) << error;
    ASSERT_EQ(loaded.meshes.size(), 1u);
    EXPECT_TRUE(loaded.meshes[0].vertices.empty());
    EXPECT_TRUE(loaded.meshes[0].faces.empty());
}

TEST_F(EditableModelTest, EmptyNativeMeshUsesAnEmptyCollider) {
    std::string error;
    const auto path = folder / "empty-collider.bmodel";
    auto mesh = quad_mesh();
    mesh.faces.clear();
    ASSERT_TRUE(commons::save_editable_model(path, {{mesh}}, error)) << error;
    commons::model model(path.string());
    commons::physics body(true, commons::physics::shapes::mesh_shape);
    body.set_model_collider_type(commons::physics::model_collider_type::concave);
    body.setModelo(&model);
    EXPECT_EQ(body.m_forma->getShapeType(), EMPTY_SHAPE_PROXYTYPE);
}

TEST_F(EditableModelTest, CopiesSelectedFaceWithUvsAndRemapsPastedGroup) {
    auto source = quad_mesh();
    source.faces = {{0, 1, 2}, {2, 3, 0}};
    source.face_groups = {{"Grupo", {0, 1}}};
    const auto copied = editor::ui::copy_mesh_elements(source, {1}, {}, {});
    ASSERT_EQ(copied.geometry.vertices.size(), 3u);
    EXPECT_EQ(copied.geometry.vertices.front().uvcoords, source.vertices[2].uvcoords);
    ASSERT_EQ(copied.geometry.faces.size(), 1u);
    ASSERT_EQ(copied.geometry.face_groups.size(), 1u);
    EXPECT_EQ(copied.geometry.face_groups.front().faces, (std::vector<unsigned int>{0}));

    auto target = quad_mesh();
    target.material_path = "target.bmat";
    std::string error;
    ASSERT_TRUE(editor::ui::append_mesh_elements(target, copied, error)) << error;
    ASSERT_EQ(target.faces.size(), 2u);
    EXPECT_EQ(target.faces.back(), (std::vector<unsigned int>{4, 5, 6}));
    ASSERT_EQ(target.face_groups.size(), 1u);
    EXPECT_EQ(target.face_groups.front().faces, (std::vector<unsigned int>{1}));
    EXPECT_EQ(target.material_path, "target.bmat");
}

TEST_F(EditableModelTest, CopiesEdgeWithIncidentFacesAndVertexWithoutFaces) {
    auto mesh = quad_mesh();
    mesh.faces = {{0, 1, 2}, {2, 3, 0}};
    const auto edge = editor::ui::copy_mesh_elements(mesh, {}, {}, {{0, 2}});
    EXPECT_EQ(edge.geometry.faces.size(), 2u);
    EXPECT_EQ(edge.edges, (std::set<editor::ui::mesh_edge>{{0, 1}}));

    const auto vertex = editor::ui::copy_mesh_elements(mesh, {}, {3}, {});
    ASSERT_EQ(vertex.geometry.vertices.size(), 1u);
    EXPECT_EQ(vertex.geometry.vertices.front().position, mesh.vertices[3].position);
    EXPECT_TRUE(vertex.geometry.faces.empty());
    std::string error;
    ASSERT_TRUE(editor::ui::append_mesh_elements(mesh, vertex, error)) << error;
    EXPECT_EQ(mesh.vertices.size(), 5u);
    EXPECT_EQ(mesh.faces.size(), 2u);
}

TEST_F(EditableModelTest, ExtractsOneNamedMeshWithEditableTopologyAndLiveMaterial) {
    auto mesh = quad_mesh();
    mesh.face_groups = {{"Painel", {0}}};
    mesh.material_path = "original.bmat";
    commons::material material;
    material.set_uniform("material.roughness", .37f);
    const auto source_texture = folder / "source.png";
    std::ofstream(source_texture, std::ios::binary) << "texture payload";
    commons::texture texture;
    texture.path = source_texture.string();
    material.set_texture("tex_albedo", texture);
    EXPECT_THROW(editor::ui::extract(folder, "original.bmodel", "../outside", mesh, material), std::runtime_error);
    const auto output = editor::ui::extract(folder, "original.bmodel", "Painel.bmodel", mesh, material);
    EXPECT_EQ(output, std::filesystem::canonical(folder) / "assets/models/original_parts/Painel.bmodel");
    material.set_uniform("material.roughness", .9f);
    EXPECT_THROW(editor::ui::extract(folder, "original.bmodel", "Painel", mesh, material), std::runtime_error);
    const auto second = editor::ui::extract(folder, "original.bmodel", "Suporte", mesh, material);
    EXPECT_EQ(second.parent_path(), output.parent_path());
    EXPECT_NE(second, output);
    ASSERT_TRUE(std::filesystem::remove(source_texture));

    commons::editable_model_data data;
    std::string error;
    ASSERT_TRUE(commons::load_editable_model(output, data, error)) << error;
    ASSERT_EQ(data.meshes.size(), 1u);
    EXPECT_EQ(data.meshes.front().faces, mesh.faces);
    ASSERT_EQ(data.meshes.front().face_groups.size(), 1u);
    EXPECT_EQ(data.meshes.front().face_groups.front().name, "Painel");
    EXPECT_EQ(mesh.material_path, "original.bmat");
    const std::filesystem::path reference(data.meshes.front().material_path);
    EXPECT_TRUE(reference.is_relative());
    const commons::model loaded(output.string());
    ASSERT_EQ(loaded.meshes.size(), 1u);
    const auto extracted = loaded.meshes.front().getMaterial();
    EXPECT_FLOAT_EQ(extracted.getUniforme("material.roughness").valor.f, .37f);
    const std::filesystem::path texture_path = extracted.getTextura("tex_albedo").path;
    EXPECT_EQ(texture_path.parent_path(), output.parent_path() / "Painel.materials");
    std::ifstream copied_texture(texture_path, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(copied_texture), {}), "texture payload");
}
