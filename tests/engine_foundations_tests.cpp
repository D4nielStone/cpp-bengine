#include <gtest/gtest.h>
#include "core/ecs.hpp"
#include "core/scene.hpp"
#include "components/camera.hpp"
#include "components/transform.hpp"
#include "components/physics.hpp"
#include "systems/physics_system.hpp"
#include "systems/code_system.hpp"
#include "guizmo/collision_debug.hpp"
#include "components/renderer.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace commons;

namespace {
template<typename T>
struct allocation_probe_allocator {
    using value_type = T;
    std::size_t* live_blocks;
    explicit allocation_probe_allocator(std::size_t& blocks) : live_blocks(&blocks) {}
    template<typename U>
    allocation_probe_allocator(const allocation_probe_allocator<U>& other)
        : live_blocks(other.live_blocks) {}
    T* allocate(std::size_t count) {
        auto* result = std::allocator<T>{}.allocate(count);
        ++*live_blocks;
        return result;
    }
    void deallocate(T* value, std::size_t count) {
        std::allocator<T>{}.deallocate(value, count);
        --*live_blocks;
    }
    bool operator==(const allocation_probe_allocator&) const = default;
};

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
    EXPECT_EQ(at(4)->reg.lock(), registry);
    expect_vector(at(4)->position, {3, 2, 1});
    unsigned count = 0;
    registry->cada<transform>([&](uint32_t id) { EXPECT_EQ(id, 4u); ++count; });
    EXPECT_EQ(count, 1u);
}

TEST(EngineEcs, StackRegistryCanCreateAndAddComponents) {
    ecs registry;
    auto object = registry.create(5);
    ASSERT_NE(registry.get<transform>(object.id), nullptr);
    EXPECT_TRUE(registry.get<transform>(object.id)->reg.expired());
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

TEST_F(ScriptLifecycle, RemovalReleasesTransformAndItsWeakReferenceControlBlock) {
    std::size_t live_blocks = 0;
    const auto object = registry->create(1);
    auto tracked = std::allocate_shared<transform>(allocation_probe_allocator<transform>{live_blocks});
    tracked->reg = registry;
    tracked->my_object = object.id;
    registry->entities.at(object.id).at(transform::mask) = tracked;
    tracked.reset();
    {
        auto entity = object;
        registry->add<code>(entity, path.string());
        code_system system;
        system.setup(registry);
        system.update(registry);
        registry->remove(object.id);
    }
    EXPECT_EQ(live_blocks, 0u);
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


TEST_F(Foundations, CharacterFallsLandsAndAcceptsImpulsesWithoutTumbling) {
    auto ground = body(1, true, {0, -0.5f, 0});
    at(1)->scale = {20, 1, 20};
    body(2, false, {0, 4, 0});
    auto player = registry->get<physics>(2);
    player->configure_collision(physics::shapes::forma_capsula, {1,1,1}, {0,0,0}, true, false);
    player->set_body_type(physics::body_type::character);
    at(2)->set_rotation({0,45,0});
    physics_system system; system.setup(registry);
    for (int i = 0; i < 300; ++i) system.world()->stepSimulation(1.f/60.f, 0);
    EXPECT_NEAR(player->m_rigid_body->getWorldTransform().getOrigin().y(), 1.4f, 0.05f);
    player->m_rigid_body->applyCentralImpulse(btVector3(2,0,0));
    EXPECT_GT(player->getVelocidade().x, 0);
    EXPECT_EQ(player->m_rigid_body->getAngularFactor(), btVector3(0,0,0));
    system.speed = 0; system.update(registry);
    EXPECT_FLOAT_EQ(at(2)->rotation.y, 45);
    player->set_body_type(physics::body_type::static_body);
    system.update(registry);
    EXPECT_TRUE(player->m_rigid_body->isStaticObject());
    player->set_body_type(physics::body_type::rigid);
    system.update(registry);
    EXPECT_FALSE(player->m_rigid_body->isStaticObject());
    EXPECT_EQ(player->m_rigid_body->getAngularFactor(), btVector3(1,1,1));
}

TEST(EnginePhysics, ColliderConfigurationRoundTripsAndRejectsInvalidValues) {
    physics source;
    source.configure_collision(physics::shapes::forma_esfera, {2,3,4}, {1,2,3}, false, true);
    source.set_body_type(physics::body_type::character);
    rapidjson::Document doc;
    source.serialize(doc, doc.GetAllocator());
    physics restored(false, physics::shapes::forma_esfera);
    ASSERT_TRUE(restored.analyze(doc));
    EXPECT_EQ(restored.type, physics::body_type::character);
    expect_vector(restored.collision_size, {2,3,4});
    expect_vector(restored.collision_offset, {1,2,3});
    EXPECT_FALSE(restored.collision_enabled);
    EXPECT_TRUE(restored.trigger);
    for (const char* json : {R"({"body_type":"ghost"})", R"({"trigger":1})",
        R"({"collision_size":[1,0,1]})", R"({"collision_offset":[1,2]})"}) {
        rapidjson::Document invalid; invalid.Parse(json);
        EXPECT_FALSE(restored.analyze(invalid));
    }
}

TEST_F(Foundations, TriggerReportsContactsWithoutBlockingAndDisabledColliderIsExcluded) {
    body(1, true, {0,0,0});
    body(2, false, {0,0.75f,0});
    auto sensor = registry->get<physics>(1);
    sensor->trigger = true;
    physics_system system; system.setup(registry); system.speed = 0; system.update(registry);
    system.world()->performDiscreteCollisionDetection();
    EXPECT_GT(system.world()->getDispatcher()->getNumManifolds(), 0);
    EXPECT_TRUE(sensor->m_rigid_body->getCollisionFlags() & btCollisionObject::CF_NO_CONTACT_RESPONSE);
    sensor->collision_enabled = false;
    system.update(registry);
    EXPECT_EQ(sensor->m_rigid_body->getBroadphaseHandle()->m_collisionFilterMask, 0);
    EXPECT_FALSE(system.cast_ray({{0,0,5},{0,0,-1}}).atingiu);
}

TEST_F(Foundations, ColliderShapeSizeAndOffsetCanChangeInWorld) {
    body(1, true, {0,0,0});
    physics_system system; system.setup(registry); system.speed = 0;
    auto collider = registry->get<physics>(1);
    collider->configure_collision(physics::shapes::box_shape, {2,2,2}, {3,0,0}, true, false);
    system.update(registry);
    EXPECT_FALSE(system.cast_ray({{0,0,5},{0,0,-1}}).atingiu);
    EXPECT_TRUE(system.cast_ray({{3,0,5},{0,0,-1}}).atingiu);
    collider->configure_collision(physics::shapes::forma_esfera, {1,1,1}, {0,0,0}, true, false);
    system.update(registry);
    EXPECT_TRUE(system.cast_ray({{0,0,5},{0,0,-1}}).atingiu);
    EXPECT_FALSE(system.cast_ray({{3,0,5},{0,0,-1}}).atingiu);
}

TEST_F(Foundations, FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn) {
    const auto scene = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "projects/fpp_demo/main.scene";
    std::ifstream input(scene);
    ASSERT_TRUE(input.good());
    const std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    rapidjson::Document document; document.Parse(json.c_str());
    ASSERT_FALSE(document.HasParseError());
    for (const auto& item : document["entities"].GetArray()) {
        if (item["id"].GetUint() != 101 && item["id"].GetUint() != 106) continue;
        auto entity = registry->create(item["id"].GetUint());
        ASSERT_TRUE(at(entity.id)->analyze(item["components"]["transform"]));
        registry->add<physics>(entity, entity.id == 101,
            entity.id == 101 ? physics::shapes::box_shape : physics::shapes::forma_capsula);
        ASSERT_TRUE(registry->get<physics>(entity.id)->analyze(item["components"]["physics"]));
    }
    physics_system system; system.setup(registry);
    for (int step = 0; step < 240; ++step) system.world()->stepSimulation(1.f/60.f, 0);
    auto player = registry->get<physics>(106);
    ASSERT_NE(player, nullptr);
    btVector3 minimum, maximum;
    player->m_rigid_body->getAabb(minimum, maximum);
    EXPECT_NEAR(minimum.y(), 0.f, 0.04f);
    EXPECT_NEAR(player->getVelocidade().y, 0.f, 0.04f);
    EXPECT_EQ(player->type, physics::body_type::character);
}

TEST_F(Foundations, CompoundRampColliderMatchesRenderedEulerRotation) {
    body(1, true, {10.393745f, 3.737701f, -3.352846f});
    at(1)->set_rotation({62.297363f, -50.768600f, 50.344112f});
    at(1)->scale = {6.252854f, 1.f, 9.099063f};
    physics_system system; system.setup(registry);
    const auto visual = at(1)->get_model_matrix();
    const auto& physical = registry->get<physics>(1)->m_rigid_body->getWorldTransform();
    for (int axis = 0; axis < 3; ++axis) {
        const auto expected = glm::normalize(glm::vec3(visual[axis]));
        const auto actual = physical.getBasis().getColumn(axis);
        EXPECT_NEAR(actual.x(), expected.x, 0.0001f) << axis;
        EXPECT_NEAR(actual.y(), expected.y, 0.0001f) << axis;
        EXPECT_NEAR(actual.z(), expected.z, 0.0001f) << axis;
    }
}

TEST_F(Foundations, RigidBodyOrientationRoundTripsCombinedEulerAngles) {
    body(1, false);
    at(1)->set_rotation({62.297363f, -50.768600f, 50.344112f});
    const auto before = at(1)->get_model_matrix();
    physics_system system; system.setup(registry); system.speed = 0; system.update(registry);
    const auto after = at(1)->get_model_matrix();
    for (int column = 0; column < 3; ++column)
        for (int row = 0; row < 3; ++row)
            EXPECT_NEAR(before[column][row], after[column][row], 0.0001f);
}

TEST_F(Foundations, FppCharacterClimbsTheRenderedRampSurface) {
    body(1, true, {10.393745f, 3.737701f, -3.352846f});
    at(1)->set_rotation({62.297363f, -50.768600f, 50.344112f});
    at(1)->scale = {6.252854f, 1.f, 9.099063f};
    const auto matrix = at(1)->get_model_matrix();
    const auto normal = glm::normalize(glm::vec3(matrix[1]));
    const auto uphill = glm::normalize(glm::vec3(0,1,0) - normal * normal.y);
    const auto top = glm::vec3(matrix * glm::vec4(0,0.5f,0,1));
    const auto spawn = top - uphill * 2.f + glm::vec3(0,0.25f,0);
    body(2, false, {spawn.x, spawn.y, spawn.z});
    auto player = registry->get<physics>(2);
    player->configure_collision(physics::shapes::forma_capsula,
        {0.7f,0.6f,0.7f}, {0,0.9f,0}, true, false);
    player->set_body_type(physics::body_type::character);
    player->setMass(80); player->setFriccao(0);
    physics_system system; system.setup(registry);
    for (int i = 0; i < 60; ++i) system.world()->stepSimulation(1.f/60.f, 0);
    const auto start = player->m_rigid_body->getWorldTransform().getOrigin();
    const auto horizontal = glm::normalize(glm::vec3(uphill.x,0,uphill.z));
    for (int i = 0; i < 90; ++i) {
        const auto velocity = player->getVelocidade();
        player->setVelocidade({horizontal.x * 2.5f, velocity.y, horizontal.z * 2.5f});
        system.world()->stepSimulation(1.f/60.f, 0);
    }
    const auto finish = player->m_rigid_body->getWorldTransform().getOrigin();
    EXPECT_GT(finish.y(), start.y() + 0.8f);
    EXPECT_GT((finish - start).dot(btVector3(uphill.x,uphill.y,uphill.z)), 2.f);
}

TEST(EnginePhysics, DefaultColliderFitsModelBoundsAndInheritsEntityScale) {
    model geometry;
    geometry.meshes.emplace_back(std::vector<vertex>{
        vertex({-2,-1,-3},{0,1,0},{0,0}), vertex({4,3,5},{0,1,0},{1,1})
    }, std::vector<unsigned int>{}, material{});
    physics collider(true);
    collider.setModelo(&geometry);
    collider.refresh_collision({2,3,4});
    btVector3 minimum, maximum;
    collider.m_rigid_body->getAabb(minimum, maximum);
    EXPECT_NEAR(minimum.x(), -4, 0.001f);
    EXPECT_NEAR(minimum.y(), -3, 0.001f);
    EXPECT_NEAR(minimum.z(), -12, 0.001f);
    EXPECT_NEAR(maximum.x(), 8, 0.001f);
    EXPECT_NEAR(maximum.y(), 9, 0.001f);
    EXPECT_NEAR(maximum.z(), 20, 0.001f);
}


TEST_F(Foundations, AutoFitColliderFollowsParentTransformAndLocalOffsets) {
    auto parent = registry->create(1);
    at(1)->set_position({7,2,-3}); at(1)->set_rotation({0,35,0}); at(1)->scale = {2,3,4};
    auto child = registry->create(2); at(2)->set_position({1,0,0});
    ASSERT_TRUE(at(2)->set_parent(1, false));
    auto geometry = new model();
    geometry->meshes.emplace_back(std::vector<vertex>{vertex({-2,-1,-3},{},{0,0}),
        vertex({4,3,5},{},{0,0})}, std::vector<unsigned int>{}, material{});
    registry->add<renderer>(child, geometry);
    registry->add<physics>(child, true);
    auto collider = registry->get<physics>(2);
    collider->collision_offset = {0.5f,1,0};
    physics_system system; system.setup(registry);
    btVector3 actual_min, actual_max;
    collider->m_rigid_body->getAabb(actual_min, actual_max);
    glm::vec3 expected_min(1e10f), expected_max(-1e10f);
    const auto matrix = at(2)->get_world_model_matrix();
    for (float x : {-2.f,4.f}) for (float y : {-1.f,3.f}) for (float z : {-3.f,5.f}) {
        const auto world = glm::vec3(matrix * glm::vec4(x+0.5f,y+1,z,1));
        expected_min = glm::min(expected_min,world); expected_max = glm::max(expected_max,world);
    }
    expect_vector({actual_min.x(),actual_min.y(),actual_min.z()}, {expected_min.x,expected_min.y,expected_min.z});
    expect_vector({actual_max.x(),actual_max.y(),actual_max.z()}, {expected_max.x,expected_max.y,expected_max.z});
    auto lines = editor::collision_lines(registry);
    ASSERT_FALSE(lines.empty());
    for (const auto& line : lines) for (const auto& p : {line.from,line.to}) {
        EXPECT_GE(p.x, actual_min.x()-0.001f); EXPECT_LE(p.x,actual_max.x()+0.001f);
        EXPECT_GE(p.y, actual_min.y()-0.001f); EXPECT_LE(p.y,actual_max.y()+0.001f);
    }
}

TEST(EnginePhysics, FitAndRotationOffsetsPersistAndLegacySizesArePreserved) {
    physics source;
    EXPECT_TRUE(source.fit_to_model);
    source.collision_rotation = {10,20,30};
    source.collision_offset = {1,2,3}; source.collision_size = {2,3,4};
    rapidjson::Document data;
    source.serialize(data,data.GetAllocator());
    physics restored; ASSERT_TRUE(restored.analyze(data));
    EXPECT_TRUE(restored.fit_to_model);
    expect_vector(restored.collision_rotation,{10,20,30});
    data.RemoveMember("fit_to_model"); data.RemoveMember("collision_rotation");
    physics legacy; ASSERT_TRUE(legacy.analyze(data)); EXPECT_FALSE(legacy.fit_to_model);
    expect_vector(legacy.collision_size,{2,3,4});
    data.AddMember("fit_to_model", "invalid", data.GetAllocator());
    EXPECT_FALSE(legacy.analyze(data));
}

TEST_F(Foundations, ColliderPreviewSupportsEveryShapeAndShowsDisabledAndTriggerColors) {
    auto object = body(1,true);
    auto collider = registry->get<physics>(1);
    for (const auto shape : {physics::shapes::box_shape,physics::shapes::forma_capsula,
        physics::shapes::forma_esfera,physics::shapes::forma_cilindro,
        physics::shapes::forma_cone,physics::shapes::forma_plano}) {
        collider->configure_collision(shape,{1,1,1},{0,0,0},true,false);
        auto lines = editor::collision_lines(registry);
        ASSERT_FALSE(lines.empty()) << static_cast<int>(shape);
        for (const auto& line : lines) for (int i = 0; i < 3; ++i)
            EXPECT_TRUE(std::isfinite(line.from[i]));
    }
    collider->trigger = true;
    auto lines = editor::collision_lines(registry);
    EXPECT_GT(lines.front().color.z, lines.front().color.y);
    collider->collision_enabled = false;
    lines = editor::collision_lines(registry);
    EXPECT_FLOAT_EQ(lines.front().color.x, lines.front().color.y);
}


TEST(EnginePhysics, RotationOffsetIsLocalAndSingularTransformDoesNotCorruptTheShape) {
    physics collider(true);
    collider.configure_collision(physics::shapes::box_shape,{2,1,4},{1,2,3},true,false);
    collider.collision_rotation = {0,90,0};
    collider.refresh_collision({2,2,2});
    btVector3 minimum,maximum; collider.m_rigid_body->getAabb(minimum,maximum);
    expect_vector({minimum.x(),minimum.y(),minimum.z()},{-2,3,4});
    expect_vector({maximum.x(),maximum.y(),maximum.z()},{6,5,8});
    transform singular; singular.scale={0,0,0};
    const auto pose = collider.synchronize_transform(singular);
    EXPECT_TRUE(std::isfinite(pose.getRotation().w()));
    collider.refresh_collision({1,1,1});
    collider.m_rigid_body->getAabb(minimum,maximum);
    EXPECT_TRUE(std::isfinite(maximum.x()));
}

namespace {
constexpr bool ramp_auto_fit = true;
// Read the primitive's real vertices without model::load_model, which uploads OpenGL buffers.
void read_collision_test_model(model& geometry, const std::filesystem::path& path) {
    std::ifstream input(path); ASSERT_TRUE(input.good());
    std::vector<vertex> vertices; std::string line;
    while (std::getline(input,line)) {
        if (!line.starts_with("v ")) continue;
        std::istringstream row(line.substr(2)); bgui::vec3 p;
        row >> p.x >> p.y >> p.z;
        vertices.emplace_back(p,bgui::vec3{0,1,0},bgui::vec2{0,0});
    }
    ASSERT_FALSE(vertices.empty());
    geometry.meshes.emplace_back(vertices,std::vector<unsigned int>{},material{});
}
}

TEST_F(Foundations, SphereContactsTheVisibleRampSurfaceWithoutSinkingIntoTheModel) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto* ramp_model = new model(); read_collision_test_model(*ramp_model,root / "commons/assets/models/cube.obj");
    auto* sphere_model = new model(); read_collision_test_model(*sphere_model,root / "commons/assets/models/sphere.obj");
    auto ramp = body(1,true,{8.9161205f,7.2638025f,-10.490142f});
    at(1)->set_rotation({27.671965f,0,0}); at(1)->scale={6.252854f,1,9.099063f};
    registry->add<renderer>(ramp,ramp_model);
    registry->get<physics>(1)->fit_to_model = ramp_auto_fit;
    auto sphere = body(2,false,{6.566506f,16.490759f,-11.271344f});
    registry->add<renderer>(sphere,sphere_model);
    auto ball = registry->get<physics>(2);
    ball->configure_collision(physics::shapes::forma_esfera,{1,1,1},{0,0,0},true,false);
    const auto matrix = at(1)->get_model_matrix();
    const auto normal = glm::normalize(glm::vec3(matrix[1]));
    const auto surface = glm::vec3(matrix * glm::vec4(0,1,0,1));
    physics_system system; system.setup(registry);
    bool hit=false;
    for (int step=0;step<360 && !hit;++step) {
        system.world()->stepSimulation(1.f/120.f,0);
        auto* dispatcher=system.world()->getDispatcher();
        for (int i=0;i<dispatcher->getNumManifolds();++i) {
            const auto* manifold=dispatcher->getManifoldByIndexInternal(i);
            for (int j=0;j<manifold->getNumContacts();++j) {
                const auto& point=manifold->getContactPoint(j);
                if (point.getDistance()>0.01f) continue;
                const auto on_ramp=manifold->getBody0()==registry->get<physics>(1)->m_rigid_body ?
                    point.getPositionWorldOnA() : point.getPositionWorldOnB();
                const auto visible_distance=glm::dot(glm::vec3(on_ramp.x(),on_ramp.y(),on_ramp.z())-surface,normal);
                EXPECT_NEAR(visible_distance,0.f,0.02f);
                hit=true;
            }
        }
    }
    EXPECT_TRUE(hit);
}

TEST_F(Foundations, CharacterIsStoppedByTheVisibleRampSide) {
    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path();
    auto* ramp_model = new model(); read_collision_test_model(*ramp_model,root / "commons/assets/models/cube.obj");
    auto ramp=body(1,true,{8.9161205f,7.2638025f,-10.490142f});
    at(1)->set_rotation({27.671965f,0,0}); at(1)->scale={6.252854f,1,9.099063f};
    registry->add<renderer>(ramp,ramp_model);
    registry->get<physics>(1)->fit_to_model = ramp_auto_fit;
    const auto matrix=at(1)->get_model_matrix();
    const auto normal=glm::normalize(glm::vec3(matrix[0]));
    const auto surface=glm::vec3(matrix*glm::vec4(1,0,0,1));
    const auto spawn=surface+normal*0.85f-glm::vec3(0,0.9f,0);
    body(2,false,{spawn.x,spawn.y,spawn.z});
    auto player=registry->get<physics>(2);
    player->configure_collision(physics::shapes::forma_capsula,{0.7f,0.6f,0.7f},{0,0.9f,0},true,false);
    player->set_body_type(physics::body_type::character);player->setMass(80);player->setFriccao(0);
    physics_system system; system.set_gravity({0,0,0});system.setup(registry);
    for (int step=0;step<240;++step) {
        player->setVelocidade({-normal.x*2.5f,0,-normal.z*2.5f});
        system.world()->stepSimulation(1.f/120.f,0);
    }
    const auto origin=player->m_rigid_body->getWorldTransform().getOrigin();
    const auto distance=glm::dot(glm::vec3(origin.x(),origin.y()+0.9f,origin.z())-surface,normal);
    EXPECT_GE(distance,0.32f);
    EXPECT_NEAR(player->getVelocidade().x,0.f,0.1f);
}

namespace {
    struct tracked_component : component {
        static constexpr component::mask mask = component::COMPONENTE_TEXTO;
        std::shared_ptr<int> resource;
        explicit tracked_component(std::shared_ptr<int> value) : resource(std::move(value)) {}
    };
}

TEST(EcsLifetime, ReleasingSceneDestroysRegistryComponentsAndResources) {
    std::weak_ptr<ecs> registry_observer;
    std::weak_ptr<component> component_observer;
    std::weak_ptr<int> resource_observer;
    {
        scene active_scene;
        auto registry = active_scene.get_ecs();
        registry_observer = registry;
        auto object = registry->create();
        auto resource = std::make_shared<int>(42);
        resource_observer = resource;
        registry->add<tracked_component>(object, resource);
        component_observer = registry->get<tracked_component>(object.id);
    }
    EXPECT_TRUE(registry_observer.expired());
    EXPECT_TRUE(component_observer.expired());
    EXPECT_TRUE(resource_observer.expired());
}

TEST(EcsLifetime, RetainedCameraAndTransformSafelyOutliveRegistry) {
    auto registry = std::make_shared<ecs>();
    auto parent = registry->create();
    auto object = registry->create();
    registry->add<camera>(object);
    auto camera_component = registry->get<camera>(object.id);
    auto transform_component = registry->get<transform>(object.id);
    ASSERT_TRUE(transform_component->set_parent(parent.id, false));
    camera_component->getViewMatrix();
    std::weak_ptr<ecs> observer = registry;
    registry.reset();
    EXPECT_TRUE(observer.expired());
    EXPECT_TRUE(camera_component->reg.expired());
    EXPECT_NO_THROW(camera_component->getViewMatrix());
    EXPECT_EQ(camera_component->m_transform, nullptr);
    EXPECT_NO_THROW(camera_component->move({1, 2, 3}));
    EXPECT_NO_THROW(transform_component->get_world_model_matrix());
    EXPECT_FALSE(transform_component->set_parent(999, false));
}

TEST(EcsLifetime, CameraResolvesRemovedAndReplacedTransform) {
    auto registry = std::make_shared<ecs>();
    auto object = registry->create();
    registry->add<camera>(object);
    auto camera_component = registry->get<camera>(object.id);
    camera_component->getViewMatrix();
    registry->remove<transform>(object.id);
    EXPECT_NO_THROW(camera_component->getViewMatrix());
    EXPECT_EQ(camera_component->m_transform, nullptr);
    registry->add<transform>(object);
    camera_component->getViewMatrix();
    EXPECT_EQ(camera_component->m_transform, registry->get<transform>(object.id).get());
}
