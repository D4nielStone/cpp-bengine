#include "rig_entity_edit.hpp"
#include "systems/rig_entities.hpp"
#include "rig_overlay.hpp"
#include "loaders/editable_model.hpp"
#include "components/renderer.hpp"
#include "systems/animation_system.hpp"
#include "model_rig_edit.hpp"
#include "mesh_topology_edit.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <limits>

namespace {
class ModelRigTest : public testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("bengine-rig-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { std::filesystem::create_directories(root); }
    void TearDown() override { std::filesystem::remove_all(root); }
    commons::editable_model_data definition() {
        commons::editable_model_data data;
        auto& mesh = data.meshes.emplace_back();
        for (const auto point : {bgui::vec3{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}})
            mesh.vertices.emplace_back(point, bgui::vec3{0,0,1}, bgui::vec2{0,0});
        mesh.faces = {{0,1,2,3}};
        mesh.skin = {{{"root",1}}, {{"root",1}}, {{"tip",1}}, {{"tip",1}}};
        data.rig.joints = {{"root", "Root", "", {}}, {"tip", "Tip", "root", {{0,1,0},{0,0,0},{1,1,1}}}};
        return data;
    }
    std::unique_ptr<commons::model> load() {
        std::string error;
        EXPECT_TRUE(commons::save_editable_model(root / "rig.bmodel", definition(), error)) << error;
        return commons::model::prepare((root / "rig.bmodel").string());
    }
};
}

TEST_F(ModelRigTest, NativeAssetPreservesBonesStableIdsParentsAndWeights) {
    std::string error;
    ASSERT_TRUE(commons::save_editable_model(root / "rig.bmodel", definition(), error)) << error;
    commons::editable_model_data restored;
    ASSERT_TRUE(commons::load_editable_model(root / "rig.bmodel", restored, error)) << error;
    ASSERT_EQ(restored.rig.joints.size(), 2u);
    EXPECT_EQ(restored.rig.joints[1].id, "tip");
    EXPECT_EQ(restored.rig.joints[1].parent, "root");
    EXPECT_EQ(restored.rig.joints[1].rest.position, (bgui::vec3{0,1,0}));
    ASSERT_EQ(restored.meshes[0].skin.size(), 4u);
    EXPECT_EQ(restored.meshes[0].skin[2][0].joint, "tip");
    EXPECT_FLOAT_EQ(restored.meshes[0].skin[2][0].weight, 1);
    auto model = load();
    EXPECT_TRUE(model->has_skeleton());
    EXPECT_EQ(model->rig_joints().size(), 2u);
    EXPECT_EQ(model->meshes[0].vertices()[2].position, (bgui::vec3{1,1,0}));
}

TEST_F(ModelRigTest, RejectsCyclesDanglingBonesAndInvalidWeightsWithoutReplacingAsset) {
    auto data = definition();
    std::string error;
    ASSERT_TRUE(commons::save_editable_model(root / "rig.bmodel", data, error));
    data.rig.joints[0].parent = "tip";
    EXPECT_FALSE(commons::save_editable_model(root / "rig.bmodel", data, error));
    data = definition(); data.meshes[0].skin[0][0].joint = "deleted";
    EXPECT_FALSE(data.validate(error));
    data = definition(); data.meshes[0].skin[0][0].weight = .3f;
    EXPECT_FALSE(data.validate(error));
    data = definition(); data.rig.joints[1].rest.scale.y = 0;
    EXPECT_FALSE(data.validate(error));
    data = definition();
    for (int i = 0; i < 20; ++i)
        data.rig.joints.push_back({"extreme-" + std::to_string(i), "Extreme", i ? "extreme-" + std::to_string(i-1) : "root",
            {{0,0,0},{0,0,0},{10000,10000,10000}}});
    EXPECT_FALSE(data.validate(error));
    ASSERT_TRUE(commons::load_editable_model(root / "rig.bmodel", data, error));
    EXPECT_TRUE(data.rig.joints[0].parent.empty());
    const auto before = data.rig.joints.size();
    std::ofstream(root / "broken.bmodel") << R"({"format":"bengine-model","version":2,"meshes":[{"vertices":[],"faces":[]}],"rig":{"joints":[{}]}})";
    EXPECT_FALSE(commons::load_editable_model(root / "broken.bmodel", data, error));
    EXPECT_EQ(data.rig.joints.size(), before);
}

TEST_F(ModelRigTest, BoneAnimationDeformsMeshWithoutMovingEntityAndClonesRemainIndependent) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, load().release());
    const auto renderer = registry->get<commons::renderer>(entity.id);
    const auto source = renderer->m_modelo->meshes[0].vertices();
    commons::animation_clip clip;
    clip.tracks.push_back({commons::renderer::mask, "rig[tip].position.x", false, {{0,0},{2,2}}});
    commons::sample_animation(*registry, entity.id, clip, 1);
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].vertices()[2].position.x, 2);
    EXPECT_EQ(renderer->m_modelo->meshes[0].vertices()[0].position, source[0].position);
    EXPECT_EQ(registry->get<commons::transform>(entity.id)->position, (bgui::vec3{0,0,0}));
    auto copy = renderer->m_modelo->clone();
    ASSERT_TRUE(renderer->write_animation_property("rig[tip].position.x", 3));
    renderer->m_modelo->apply_pose();
    EXPECT_FLOAT_EQ(copy->meshes[0].vertices()[2].position.x, 2);
    EXPECT_FALSE(renderer->write_animation_property("rig[missing].position.x", 1));
    EXPECT_FALSE(renderer->write_animation_property("rig[tip].scale.y", 0));
    EXPECT_DOUBLE_EQ(*renderer->read_animation_property("rig[tip].position.x"), 3);
}

TEST_F(ModelRigTest, RotationUsesJointBindPositionAndScenePoseRoundTrips) {
    auto model = load();
    commons::renderer renderer(model.release());
    ASSERT_TRUE(renderer.write_animation_property("rig[tip].rotation.z", 90));
    renderer.m_modelo->apply_pose();
    const auto vertex = renderer.m_modelo->meshes[0].vertices()[2].position;
    EXPECT_NEAR(vertex.x, 0, .0001f);
    EXPECT_NEAR(vertex.y, 2, .0001f);
    rapidjson::Document encoded(rapidjson::kObjectType);
    ASSERT_TRUE(renderer.serialize(encoded, encoded.GetAllocator()));
    ASSERT_TRUE(encoded.HasMember("rig_pose"));
    commons::renderer restored;
    ASSERT_TRUE(restored.analyze(encoded));
    EXPECT_DOUBLE_EQ(*restored.read_animation_property("rig[tip].rotation.z"), 90);
    EXPECT_NEAR(restored.m_modelo->meshes[0].vertices()[2].position.y, 2, .0001f);
    encoded["rig_pose"][0]["scale"][0].SetFloat(0);
    const auto* existing = restored.m_modelo;
    EXPECT_FALSE(restored.analyze(encoded));
    EXPECT_EQ(restored.m_modelo, existing);
    EXPECT_DOUBLE_EQ(*restored.read_animation_property("rig[tip].rotation.z"), 90);
}

TEST_F(ModelRigTest, AutomaticAndManualWeightsRemainNormalizedThroughTopologyEdits) {
    auto data = definition();
    std::string error;
    ASSERT_TRUE(editor::ui::bind_model_rig(data, error)) << error;
    for (const auto& weights : data.meshes[0].skin) {
        float sum = 0; for (const auto& weight : weights) sum += weight.weight;
        EXPECT_NEAR(sum, 1, .0001f);
    }
    ASSERT_TRUE(editor::ui::set_rig_vertex_weight(data, 0, {2}, "root", .25f, error)) << error;
    const auto& weights = data.meshes[0].skin[2];
    const auto root = std::find_if(weights.begin(), weights.end(), [](const auto& w) { return w.joint == "root"; });
    ASSERT_NE(root, weights.end());
    EXPECT_FLOAT_EQ(root->weight, .25f);
    ASSERT_TRUE(editor::ui::extrude_faces(data.meshes[0], {0}, .5f, error)) << error;
    EXPECT_EQ(data.meshes[0].skin.size(), data.meshes[0].vertices.size());
    ASSERT_TRUE(data.validate(error)) << error;
    const auto copy = editor::ui::copy_mesh_elements(data.meshes[0], {0}, {}, {});
    ASSERT_FALSE(copy.geometry.skin.empty());
    ASSERT_TRUE(editor::ui::append_mesh_elements(data.meshes[0], copy, error)) << error;
    ASSERT_TRUE(data.validate(error)) << error;
    const auto last = data.meshes[0].vertices.size() - 1;
    ASSERT_TRUE(editor::ui::delete_mesh_elements(data.meshes[0], {}, {last}, {}, error)) << error;
    ASSERT_TRUE(data.validate(error)) << error;
}

TEST_F(ModelRigTest, WeightBrushSweepsFastDragsWithSmoothFalloff) {
    using editor::ui::rig_brush_influence;
    EXPECT_FLOAT_EQ(rig_brush_influence({0,0}, {0,0}, {0,0}, 20, .5f), .5f);
    EXPECT_FLOAT_EQ(rig_brush_influence({50,0}, {0,0}, {100,0}, 20, .5f), .5f);
    EXPECT_FLOAT_EQ(rig_brush_influence({50,10}, {0,0}, {100,0}, 20, .5f), .25f);
    EXPECT_FLOAT_EQ(rig_brush_influence({50,20}, {0,0}, {100,0}, 20, .5f), 0);
    EXPECT_FLOAT_EQ(rig_brush_influence({0,0}, {0,0}, {0,0}, 0, .5f), 0);
    EXPECT_FLOAT_EQ(rig_brush_influence({0,0}, {0,0}, {0,0}, 20, 2), 0);
}

TEST_F(ModelRigTest, PaintBatchPreservesRatiosLimitsInfluencesAndRejectsInvalidValuesAtomically) {
    auto data = definition();
    std::string error;
    ASSERT_TRUE(editor::ui::set_rig_vertex_weights(data, 0, {{0,.25f},{1,.75f}}, "tip", error)) << error;
    EXPECT_FLOAT_EQ(editor::ui::rig_vertex_weight(data.meshes[0].skin[0], "tip"), .25f);
    EXPECT_FLOAT_EQ(editor::ui::rig_vertex_weight(data.meshes[0].skin[1], "root"), .25f);
    ASSERT_TRUE(commons::save_editable_model(root / "paint.bmodel", data, error)) << error;
    commons::editable_model_data restored;
    ASSERT_TRUE(commons::load_editable_model(root / "paint.bmodel", restored, error)) << error;
    EXPECT_FLOAT_EQ(editor::ui::rig_vertex_weight(restored.meshes[0].skin[0], "tip"), .25f);
    EXPECT_FALSE(editor::ui::set_rig_vertex_weights(data, 0, {{0,.8f},{99,.2f}}, "tip", error));
    EXPECT_FLOAT_EQ(editor::ui::rig_vertex_weight(data.meshes[0].skin[0], "tip"), .25f);
    EXPECT_FALSE(editor::ui::set_rig_vertex_weights(data, 0, {{0,std::numeric_limits<float>::quiet_NaN()}}, "tip", error));
    for (int i = 0; i < 7; ++i) data.rig.joints.push_back({"extra-" + std::to_string(i), "Extra", "root", {}});
    auto& weights = data.meshes[0].skin[0];
    weights = {{"root",.125f}};
    for (int i = 0; i < 7; ++i) weights.push_back({"extra-" + std::to_string(i), .125f});
    ASSERT_TRUE(data.validate(error)) << error;
    ASSERT_TRUE(editor::ui::set_rig_vertex_weights(data, 0, {{0,.3f}}, "tip", error)) << error;
    EXPECT_EQ(data.meshes[0].skin[0].size(), 8u);
    EXPECT_FLOAT_EQ(editor::ui::rig_vertex_weight(data.meshes[0].skin[0], "tip"), .3f);
    EXPECT_TRUE(data.validate(error)) << error;
}

TEST_F(ModelRigTest, SkeletonEntitiesPreserveHierarchyAnimateAndSurviveOwnerRemoval) {
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::renderer>(entity, load().release());
    const auto bones = commons::create_rig_entities(*registry, entity.id);
    ASSERT_EQ(bones.size(), 2u);
    EXPECT_TRUE(commons::create_rig_entities(*registry, entity.id).empty());
    const auto root_bone = registry->get<commons::transform>(bones[0]);
    const auto tip = registry->get<commons::transform>(bones[1]);
    EXPECT_EQ(root_bone->parent_id, entity.id);
    EXPECT_EQ(tip->parent_id, bones[0]);
    EXPECT_EQ(tip->get_world_position(), (bgui::vec3{0,1,0}));
    tip->set_world_position({2,1,0});
    EXPECT_EQ(tip->position, (bgui::vec3{2,0,0}));
    commons::synchronize_rig_entities(*registry);
    auto renderer = registry->get<commons::renderer>(entity.id);
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].vertices()[2].position.x, 3);
    commons::animation_clip clip;
    clip.tracks.push_back({commons::transform::mask, "position.x", false, {{0,0},{2,2}}});
    commons::sample_animation(*registry, bones[1], clip, 1);
    EXPECT_FLOAT_EQ(renderer->m_modelo->meshes[0].vertices()[2].position.x, 2);
    EXPECT_TRUE(renderer->m_modelo->set_joint_pose("tip", {{4,0,0},{0,0,0},{1,1,1}}));
    commons::synchronize_rig_entities(*registry);
    EXPECT_FLOAT_EQ(tip->position.x, 4);
    registry->remove(entity.id);
    EXPECT_FALSE(commons::rig_entity_renderer(*tip));
    EXPECT_NO_THROW(commons::synchronize_rig_entities(*registry));
}

TEST_F(ModelRigTest, JointBindingSerializesWithoutEntityIdsAndWorldRotationUsesRest) {
    auto data = definition();
    data.rig.joints[1].rest.rotation = {0,0,90};
    std::string error;
    ASSERT_TRUE(commons::save_editable_model(root / "rotated.bmodel", data, error));
    auto registry = std::make_shared<commons::ecs>();
    auto owner = registry->create();
    registry->add<commons::renderer>(owner, commons::model::prepare((root / "rotated.bmodel").string()).release());
    const auto bones = commons::create_rig_entities(*registry, owner.id);
    const auto bone = registry->get<commons::transform>(bones[1]);
    bone->set_world_rotation({0,0,120});
    EXPECT_NEAR(bone->rotation.z, 30, .001f);
    EXPECT_FALSE(bone->set_parent(owner.id));
    rapidjson::Document value(rapidjson::kObjectType);
    ASSERT_TRUE(bone->serialize(value, value.GetAllocator()));
    commons::transform restored;
    ASSERT_TRUE(restored.analyze(value));
    EXPECT_EQ(restored.rig_joint_id, "tip");
    EXPECT_EQ(restored.position, bone->position);
    value["rig_joint"].SetInt(3);
    EXPECT_FALSE(restored.analyze(value));
}

TEST(RigOverlay, InterpolatesWeightColorsAndClipsTriangles) {
    bgui::draw_list draw;
    editor::ui::draw_weight_triangle(draw, {{{-10,0},editor::ui::rig_weight_color(0)},
        {{10,0},editor::ui::rig_weight_color(1)}, {{10,20},editor::ui::rig_weight_color(1)}}, {0,0,10,10});
    ASSERT_FALSE(draw.get_vertices().empty());
    bool blended = false;
    for (const auto& vertex : draw.get_vertices()) {
        EXPECT_GE(vertex.m_position.x, 0); EXPECT_LE(vertex.m_position.x, 10);
        EXPECT_GE(vertex.m_position.y, 0); EXPECT_LE(vertex.m_position.y, 10);
        blended |= vertex.m_color.x > 0 && vertex.m_color.x < 1;
    }
    EXPECT_TRUE(blended);
    draw.clear();
    draw.set_clip_rect({0,0,100,100});
    editor::ui::draw_rig_segment(draw, glm::mat4{1}, {0,0,100,100}, {0,0,0}, {0,.5f,0});
    EXPECT_GE(draw.get_vertices().size(), 48u);
}

TEST_F(ModelRigTest, ExposingSkeletonIsOneUndoableActionWithStableIds) {
    auto context = std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());
    auto owner = context->registry->create();
    context->registry->add<commons::renderer>(owner, load().release());
    ASSERT_TRUE(editor::ui::expose_rig_entities(context, owner.id));
    ASSERT_EQ(context->undo_stack.size(), 1u);
    const auto names = context->entity_names;
    ASSERT_EQ(names.size(), 2u);
    EXPECT_FALSE(editor::ui::expose_rig_entities(context, owner.id));
    ASSERT_TRUE(context->undo());
    EXPECT_EQ(context->registry->entities.size(), 1u);
    EXPECT_TRUE(context->entity_names.empty());
    ASSERT_TRUE(context->redo());
    EXPECT_EQ(context->entity_names, names);
    for (const auto& [id, name] : names)
        EXPECT_TRUE(commons::rig_entity_renderer(*context->registry->get<commons::transform>(id)));
}
