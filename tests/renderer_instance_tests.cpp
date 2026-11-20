#include <gtest/gtest.h>
#include "components/renderer.hpp"
#include <rapidjson/document.h>

TEST(RendererInstances, SerializesAndRestoresWorldTransformsAtomically) {
    auto geometry = new commons::model();
    geometry->meshes.emplace_back(std::vector<commons::vertex>{},std::vector<unsigned>{},commons::material{});
    commons::renderer renderer(geometry);
    rapidjson::Document source;
    source.Parse(R"({"mesh_instances":[[{"position":[3,2,1],"rotation":[0,45,0],"scale":[2,1,2]}]]})");
    ASSERT_TRUE(renderer.analyze_instances(source));
    ASSERT_EQ(geometry->meshes[0].instances().size(),1u);
    EXPECT_EQ(geometry->meshes[0].instances()[0].get_position(),(bgui::vec3{3,2,1}));
    rapidjson::Document saved(rapidjson::kObjectType);
    ASSERT_TRUE(renderer.serialize_instances(saved,saved.GetAllocator()));
    EXPECT_FLOAT_EQ(saved["mesh_instances"][0][0]["rotation"][1].GetFloat(),45);
    for (const auto* text : {R"({"mesh_instances":[[{"position":7}]]})",R"({"mesh_instances":false})",R"({"mesh_instances":[[],[]]})"}) {
        rapidjson::Document invalid;invalid.Parse(text);
        EXPECT_FALSE(renderer.analyze_instances(invalid));
        EXPECT_EQ(geometry->meshes[0].instances().size(),1u);
    }
    rapidjson::Document cleared;cleared.Parse(R"({"mesh_instances":[[]]})");
    ASSERT_TRUE(renderer.analyze_instances(cleared));
    EXPECT_TRUE(geometry->meshes[0].instances().empty());
}

TEST(RendererInstances, MeshValueCopiesOwnTheirTransformTargets) {
    commons::mesh mesh;
    std::vector<commons::transform> source(1);
    source[0].set_position({4,5,6});
    mesh.setInstances(source);
    EXPECT_NE(mesh.instances()[0].target,source[0].target);
    auto copied=mesh.getInstances();
    EXPECT_NE(copied[0].target,mesh.instances()[0].target);
    source.clear();
    EXPECT_EQ(copied[0].get_position(),(bgui::vec3{4,5,6}));
}
