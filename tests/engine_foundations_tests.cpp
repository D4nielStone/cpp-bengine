#include <gtest/gtest.h>
#include "core/ecs.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "systems/physics_system.hpp"
#include "systems/code_system.hpp"
#include <filesystem>
#include <fstream>

using namespace commons;

namespace {
// Components own the registry in the current API; explicitly tear down test scenes.
struct Foundations : testing::Test {
    std::shared_ptr<ecs> registry = std::make_shared<ecs>();
    void TearDown() override {
        const auto ids = registry->live_entities;
        for (const auto id : ids) registry->remove(id);
    }
    std::shared_ptr<transform> at(uint32_t id) { return registry->get<transform>(id); }
    entity body(uint32_t id, bool is_static, bgui::vec3 position = {}) {
        auto result = registry->create(id);
        at(id)->set_position(position);
        registry->add<physics>(result, is_static);
        return result;
    }
};
void expect_vector(bgui::vec3 actual, bgui::vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 1e-4f);
    EXPECT_NEAR(actual.y, expected.y, 1e-4f);
    EXPECT_NEAR(actual.z, expected.z, 1e-4f);
}
}

TEST_F(Foundations, DuplicateAndAutomaticIdsNeverReplaceLivingEntities) {
    auto first = registry->create(1);
    at(first.id)->position = {7, 8, 9};
    registry->remove<transform>(first.id);
    const auto duplicate = registry->create(1);
    const auto automatic = registry->create();
    EXPECT_NE(duplicate.id, first.id);
    EXPECT_NE(automatic.id, first.id);
    EXPECT_NE(automatic.id, duplicate.id);
    EXPECT_TRUE(registry->is_alive(first.id));
    EXPECT_FALSE(registry->has<transform>(first.id));
    EXPECT_TRUE(registry->validate_invariants());
}

TEST_F(Foundations, MissingEntityOperationsDoNotCreateStorage) {
    entity absent{987};
    registry->add<transform>(absent);
    registry->remove<transform>(absent.id);
    registry->remove(absent.id);
    EXPECT_EQ(registry->get<transform>(absent.id), nullptr);
    EXPECT_EQ(registry->get_components(absent.id), component::COMPONENTE_NONE);
    EXPECT_TRUE(registry->entities.empty());
    EXPECT_TRUE(registry->mascaras.empty());
    EXPECT_TRUE(registry->live_entities.empty());
}

TEST_F(Foundations, ReplacingComponentUpdatesLookupAndIterationWithoutDuplicates) {
    auto object = registry->create(4);
    const auto old = at(4);
    registry->add<transform>(object, bgui::vec3{3, 2, 1});
    EXPECT_NE(at(4), old);
    EXPECT_EQ(at(4)->my_object, 4u);
    EXPECT_EQ(at(4)->reg, registry);
    expect_vector(at(4)->position, {3, 2, 1});
    unsigned count = 0;
    registry->cada<transform>([&](uint32_t id) { EXPECT_EQ(id, 4u); ++count; });
    EXPECT_EQ(count, 1u);
}

TEST(EngineEcs, StackRegistryCanCreateAndAddComponents) {
    ecs registry;
    auto object = registry.create(5);
    ASSERT_NE(registry.get<transform>(object.id), nullptr);
    EXPECT_EQ(registry.get<transform>(object.id)->reg, nullptr);
    registry.add<physics>(object, true);
    EXPECT_TRUE(registry.has<physics>(object.id));
    EXPECT_TRUE(registry.validate_invariants());
}

TEST_F(Foundations, RemovingEntityReleasesComponentsAndItsName) {
    const auto object = registry->create(9);
    std::weak_ptr<transform> component = at(9);
    registry->entity_names[9] = "Player";
    registry->remove(object.id);
    EXPECT_TRUE(component.expired());
    EXPECT_EQ(registry->find_by_name("Player"), std::nullopt);
    EXPECT_TRUE(registry->entity_names.empty());
    registry->create(9);
    EXPECT_EQ(registry->find_by_name("Player"), std::nullopt);
}

TEST_F(Foundations, HierarchyComposesTranslationRotationAndScale) {
    registry->create(1); registry->create(2);
    at(1)->position = {10, 0, 0};
    at(1)->rotation = {0, 0, 90};
    at(1)->scale = {2, 2, 2};
    at(2)->position = {1, 0, 0};
    ASSERT_TRUE(at(2)->set_parent(1, false));
    expect_vector(at(2)->get_world_position(), {10, 2, 0});
    at(2)->set_world_position({10, 6, 0});
    expect_vector(at(2)->position, {3, 0, 0});
    expect_vector(at(2)->get_world_position(), {10, 6, 0});
}

TEST_F(Foundations, ReparentAndDetachPreserveWorldPosition) {
    registry->create(1); registry->create(2);
    at(1)->position = {10, -3, 7};
    at(2)->position = {4, 5, 6};
    ASSERT_TRUE(at(2)->set_parent(1));
    expect_vector(at(2)->get_world_position(), {4, 5, 6});
    expect_vector(at(2)->position, {-6, 8, -1});
    ASSERT_TRUE(at(2)->set_parent(0));
    expect_vector(at(2)->position, {4, 5, 6});
}

TEST_F(Foundations, InvalidParentsAndCyclesLeaveHierarchyUnchanged) {
    registry->create(1); registry->create(2); registry->create(3);
    ASSERT_TRUE(at(2)->set_parent(1, false));
    ASSERT_TRUE(at(3)->set_parent(2, false));
    EXPECT_FALSE(at(1)->set_parent(3));
    EXPECT_FALSE(at(2)->set_parent(2));
    EXPECT_FALSE(at(2)->set_parent(999));
    EXPECT_EQ(at(1)->parent_id, 0u);
    EXPECT_EQ(at(2)->parent_id, 1u);
    EXPECT_EQ(at(3)->parent_id, 2u);
}

TEST_F(Foundations, SingularParentRejectsWorldPreservationWithoutMutation) {
    registry->create(1); registry->create(2);
    at(1)->scale = {0, 1, 1};
    at(2)->position = {2, 3, 4};
    EXPECT_FALSE(at(2)->set_parent(1));
    EXPECT_EQ(at(2)->parent_id, 0u);
    expect_vector(at(2)->position, {2, 3, 4});
    ASSERT_TRUE(at(2)->set_parent(1, false));
    at(2)->set_world_position({8, 9, 10});
    expect_vector(at(2)->position, {2, 3, 4});
}

TEST_F(Foundations, TransformSerializationRoundTripsLocalTrs) {
    transform original({1, -2, 3}, {15, 30, -45}, {2, 3, 4});
    rapidjson::Document document;
    document.SetObject();
    ASSERT_TRUE(original.serialize(document, document.GetAllocator()));
    transform restored;
    ASSERT_TRUE(restored.analyze(document));
    expect_vector(restored.position, original.position);
    expect_vector(restored.rotation, original.rotation);
    expect_vector(restored.scale, original.scale);
}

TEST_F(Foundations, PhysicsSetupIsIdempotentAndScalesCollisionShapes) {
    body(1, true, {3, 4, 5});
    at(1)->scale = {-2, 3, 4};
    physics_system system;
    system.set_gravity({0, -3, 0});
    system.setup(registry); system.setup(registry);
    ASSERT_NE(system.world(), nullptr);
    EXPECT_EQ(system.world()->getNumCollisionObjects(), 1);
    EXPECT_FLOAT_EQ(system.world()->getGravity().y(), -3);
    const auto physics = registry->get<commons::physics>(1);
    EXPECT_FLOAT_EQ(physics->m_forma->getLocalScaling().x(), 2);
    EXPECT_FLOAT_EQ(physics->m_forma->getLocalScaling().y(), 3);
    EXPECT_FLOAT_EQ(physics->m_rigid_body->getWorldTransform().getOrigin().z(), 5);
    system.set_gravity({0, -6, 0});
    EXPECT_FLOAT_EQ(system.world()->getGravity().y(), -6);
}

TEST_F(Foundations, PhysicsSynchronizesAddedReplacedAndRemovedBodies) {
    body(1, true);
    physics_system system;
    system.speed = 0;
    system.setup(registry);
    auto second = body(2, true);
    system.update(registry);
    EXPECT_EQ(system.world()->getNumCollisionObjects(), 2);
    const auto old = registry->get<physics>(2);
    registry->add<physics>(second, true);
    system.update(registry);
    EXPECT_EQ(system.world()->getNumCollisionObjects(), 2);
    const auto& objects = system.world()->getCollisionObjectArray();
    EXPECT_EQ(objects.findLinearSearch(old->m_rigid_body), objects.size());
    registry->remove<transform>(1);
    registry->remove(2);
    system.update(registry);
    EXPECT_EQ(system.world()->getNumCollisionObjects(), 0);
}

TEST_F(Foundations, StaticBodyFollowsEditedTransformWhileSimulationIsPaused) {
    body(1, true);
    physics_system system;
    system.speed = 0;
    system.setup(registry);
    at(1)->position = {7, 8, 9};
    system.update(registry);
    const auto origin = registry->get<physics>(1)->m_rigid_body->getWorldTransform().getOrigin();
    EXPECT_FLOAT_EQ(origin.x(), 7);
    EXPECT_FLOAT_EQ(origin.y(), 8);
    EXPECT_FLOAT_EQ(origin.z(), 9);
}

TEST_F(Foundations, RaycastReturnsClosestBodyPointNormalAndMiss) {
    body(1, true, {0, 0, 0}); body(2, true, {0, 0, -4});
    physics_system system;
    EXPECT_FALSE(system.cast_ray({{0, 0, 5}, {0, 0, -1}}).atingiu);
    system.setup(registry);
    const auto hit = system.cast_ray({{0, 0, 5}, {0, 0, -1}});
    ASSERT_TRUE(hit.atingiu);
    EXPECT_EQ(hit.objetoAtingido, registry->get<physics>(1)->m_rigid_body);
    expect_vector(hit.pontoDeColisao, {0, 0, 0.5f});
    expect_vector(hit.normalAtingida, {0, 0, 1});
    const auto miss = system.cast_ray({{10, 0, 5}, {0, 0, -1}});
    EXPECT_FALSE(miss.atingiu);
    EXPECT_EQ(miss.objetoAtingido, nullptr);
}

TEST_F(Foundations, BulletFixedStepsApplyGravityAndResolveGroundContact) {
    auto ground = body(1, true, {0, -0.5f, 0});
    at(ground.id)->scale = {20, 1, 20};
    body(2, false, {0, 5, 0});
    physics_system system;
    system.setup(registry);
    // Fixed Bullet steps avoid sleeps and wall-clock-dependent assertions.
    for (int step = 0; step < 30; ++step)
        system.world()->stepSimulation(1.f / 60.f, 0);
    const auto falling = registry->get<physics>(2);
    EXPECT_LT(falling->getVelocidade().y, -1.f);
    for (int step = 0; step < 570; ++step)
        system.world()->stepSimulation(1.f / 60.f, 0);
    system.speed = 0;
    system.update(registry);
    EXPECT_NEAR(at(2)->position.y, 0.5f, 0.03f);
    EXPECT_NEAR(falling->getVelocidade().y, 0.f, 0.03f);
}

class PhysicsShapeContract : public testing::TestWithParam<physics::shapes> {};
TEST_P(PhysicsShapeContract, CreatesUsableBodyAndSerializesMaterialProperties) {
    physics source(false, GetParam());
    source.setMass(3);
    source.setFriccao(0.7f);
    source.setRestituicao(0.4f);
    ASSERT_NE(source.m_forma, nullptr);
    ASSERT_NE(source.m_rigid_body, nullptr);
    rapidjson::Document document;
    ASSERT_TRUE(source.serialize(document, document.GetAllocator()));
    EXPECT_FALSE(document["static"].GetBool());
    EXPECT_TRUE(document["shape"].IsString());
    physics restored(false, GetParam());
    ASSERT_TRUE(restored.analyze(document));
    EXPECT_FLOAT_EQ(restored.m_rigid_body->getInvMass(), 1.f / 3.f);
    EXPECT_FLOAT_EQ(restored.m_rigid_body->getFriction(), 0.7f);
    EXPECT_FLOAT_EQ(restored.m_rigid_body->getRestitution(), 0.4f);
}
INSTANTIATE_TEST_SUITE_P(SupportedShapes, PhysicsShapeContract, testing::Values(
    physics::shapes::box_shape, physics::shapes::forma_capsula,
    physics::shapes::forma_esfera, physics::shapes::forma_cilindro,
    physics::shapes::forma_cone, physics::shapes::forma_plano));

TEST(EnginePhysics, InvalidInputsAreRejected) {
    EXPECT_THROW(physics(false, physics::shapes::mesh_shape), std::invalid_argument);
    physics body;
    EXPECT_THROW(body.setModelo(nullptr), std::invalid_argument);
    for (const auto* json : {"null", "[]", R"({"mass":"heavy"})",
                            R"({"friction":false})", R"({"restitution":[]})"}) {
        SCOPED_TRACE(json);
        rapidjson::Document document;
        document.Parse(json);
        ASSERT_FALSE(document.HasParseError());
        EXPECT_FALSE(body.analyze(document));
    }
}

TEST(EnginePhysics, ForcesVelocityAndAxisLocksReachBullet) {
    physics body;
    body.setVelocidade({1, 2, 3});
    expect_vector(body.getVelocidade(), {1, 2, 3});
    body.setForca({4, 5, 6});
    EXPECT_FLOAT_EQ(body.m_rigid_body->getTotalForce().y(), 5);
    body.setFatorLinear({1, 0, 1});
    body.setFatorAngular({0, 1, 0});
    EXPECT_EQ(body.m_rigid_body->getLinearFactor(), btVector3(1, 0, 1));
    EXPECT_EQ(body.m_rigid_body->getAngularFactor(), btVector3(0, 1, 0));
    body.setMass(0);
    EXPECT_TRUE(body.m_rigid_body->isStaticObject());
    body.setMass(2);
    EXPECT_FALSE(body.m_rigid_body->isStaticObject());
    EXPECT_FLOAT_EQ(body.m_rigid_body->getInvMass(), 0.5f);
}

namespace {
struct ScriptLifecycle : Foundations {
    std::filesystem::path path;
    void SetUp() override {
        path = std::filesystem::temp_directory_path() /
            ("bengine-foundations-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua");
        std::ofstream file(path);
        file << R"(
function setup() setups = (setups or 0) + 1; updates = 0 end
function update() updates = updates + 1 end
)";
        ASSERT_TRUE(file.good());
    }
    void TearDown() override {
        Foundations::TearDown();
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
}

TEST_F(ScriptLifecycle, LateAttachmentInitializesOnceAndRemovalStopsUpdates) {
    auto object = registry->create(1);
    code_system system;
    system.setup(registry);
    registry->add<code>(object, path.string());
    const auto script = registry->get<code>(1);
    system.update(registry);
    system.update(registry);
    EXPECT_EQ(script->estado_lua["setups"].get<int>(), 1);
    EXPECT_EQ(script->estado_lua["updates"].get<int>(), 2);
    registry->remove<code>(1);
    system.update(registry);
    EXPECT_EQ(script->estado_lua["updates"].get<int>(), 2);
}

TEST_F(ScriptLifecycle, MultipleScriptsKeepIndependentStatesAndReplacementInitializes) {
    auto object = registry->create(1);
    registry->add<code>(object, path.string());
    const auto first = registry->get<code>(1);
    const auto second = std::make_shared<code>(path.string());
    first->scripts.push_back(second);
    code_system system;
    system.setup(registry);
    system.update(registry);
    EXPECT_EQ(first->estado_lua["setups"].get<int>(), 1);
    EXPECT_EQ(second->estado_lua["setups"].get<int>(), 1);
    first->estado_lua["updates"] = 40;
    system.update(registry);
    EXPECT_EQ(first->estado_lua["updates"].get<int>(), 41);
    EXPECT_EQ(second->estado_lua["updates"].get<int>(), 2);
    registry->add<code>(object, path.string());
    system.update(registry);
    const auto replacement = registry->get<code>(1);
    EXPECT_EQ(replacement->estado_lua["setups"].get<int>(), 1);
    EXPECT_EQ(replacement->estado_lua["updates"].get<int>(), 1);
    EXPECT_EQ(first->estado_lua["updates"].get<int>(), 41);
}
