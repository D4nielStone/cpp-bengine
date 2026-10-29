#include "systems/physics_system.hpp"

#include "components/transform.hpp"
#include "components/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtx/euler_angles.hpp>

namespace commons {
namespace {

constexpr float radians_to_degrees = 57.295779513082320876f;

btTransform entity_pose(physics& body, const transform& value, const std::shared_ptr<ecs>& registry, uint32_t id) {
    const auto visual = registry->get<renderer>(id);
    return body.synchronize_transform(value, visual ? visual->m_modelo : nullptr);
}

}

physics_system::physics_system()
    : speed(1.f) {}

physics_system::~physics_system() {
    if (dynamic_world) {
        for (const auto& [id, body] : m_bodies) {
            (void)id;
            if (body && body->m_rigid_body)
                dynamic_world->removeRigidBody(body->m_rigid_body);
        }
    }
    m_bodies.clear();
    delete dynamic_world;
    delete solucionador;
    delete faseAmpla;
    delete expedidor;
    delete configColisao;
}

void physics_system::ensure_world() {
    if (dynamic_world)
        return;

    configColisao = new btDefaultCollisionConfiguration();
    expedidor = new btCollisionDispatcher(configColisao);
    faseAmpla = new btDbvtBroadphase();
    solucionador = new btSequentialImpulseConstraintSolver();
    dynamic_world = new btDiscreteDynamicsWorld(
        expedidor,
        faseAmpla,
        solucionador,
        configColisao
    );
    dynamic_world->setGravity(btVector3(m_gravity.x, m_gravity.y, m_gravity.z));
}

void physics_system::synchronize_entities(
    const std::shared_ptr<ecs>& registry
) {
    if (!registry || !dynamic_world)
        return;

    for (auto body = m_bodies.begin(); body != m_bodies.end();) {
        const auto current = registry->get<physics>(body->first);
        const auto transform_component =
            registry->get<transform>(body->first);
        if (current != body->second || !transform_component) {
            if (body->second && body->second->m_rigid_body)
                dynamic_world->removeRigidBody(body->second->m_rigid_body);
            m_collision_revisions.erase(body->first);
            body = m_bodies.erase(body);
        } else {
            ++body;
        }
    }

    registry->cada<physics, transform>(
        [this, &registry](const std::uint32_t id) {
            auto body = registry->get<physics>(id);
            auto entity_transform = registry->get<transform>(id);
            if (!body || !entity_transform || m_bodies.contains(id))
                return;

            const auto world_transform = entity_pose(*body, *entity_transform, registry, id);
            body->m_rigid_body->setWorldTransform(world_transform);
            body->m_estado_de_movimento->setWorldTransform(world_transform);
            dynamic_world->addRigidBody(body->m_rigid_body, 1, body->collision_enabled ? -1 : 0);
            m_collision_revisions[id] = body->collision_revision;
            m_bodies.emplace(id, std::move(body));
        }
    );
}

void physics_system::setup(const std::shared_ptr<ecs>& registry) {
    ensure_world();
    synchronize_entities(registry);
    m_last_update = std::chrono::steady_clock::now();
}

void physics_system::update(const std::shared_ptr<ecs>& registry) {
    ensure_world();
    synchronize_entities(registry);
    if (!registry || !dynamic_world)
        return;

    for (const auto& [id, body] : m_bodies) {
        const auto tr = registry->get<transform>(id);
        const auto desired_pose = entity_pose(*body, *tr, registry, id);
        auto* proxy = body->m_rigid_body->getBroadphaseHandle();
        const int group = body->m_massa <= 0.f ? btBroadphaseProxy::StaticFilter : btBroadphaseProxy::DefaultFilter;
        const int filter = body->collision_enabled ? btBroadphaseProxy::AllFilter : 0;
        if (proxy && (proxy->m_collisionFilterGroup != group || proxy->m_collisionFilterMask != filter ||
            m_collision_revisions[id] != body->collision_revision)) {
            m_collision_revisions[id] = body->collision_revision;
            dynamic_world->removeRigidBody(body->m_rigid_body);
            dynamic_world->addRigidBody(body->m_rigid_body, group, filter);
        }
        if (body->type == physics::body_type::character) {
            auto pose = body->m_rigid_body->getWorldTransform();
            pose.setRotation(desired_pose.getRotation());
            body->m_rigid_body->setWorldTransform(pose);
            body->m_estado_de_movimento->setWorldTransform(pose);
        }
        dynamic_world->updateSingleAabb(body->m_rigid_body);
        if (!body || body->m_massa > 0.f)
            continue;
        const auto entity_transform = registry->get<transform>(id);
        if (!entity_transform)
            continue;
        const auto world_transform = desired_pose;
        body->m_rigid_body->setWorldTransform(world_transform);
        body->m_estado_de_movimento->setWorldTransform(world_transform);
        dynamic_world->updateSingleAabb(body->m_rigid_body);
    }

    const auto now = std::chrono::steady_clock::now();
    const float elapsed = m_last_update.time_since_epoch().count() == 0
        ? 0.f
        : std::chrono::duration<float>(now - m_last_update).count();
    m_last_update = now;
    const float delta_time = std::clamp(elapsed * speed, 0.f, 0.1f);
    if (delta_time > 0.f)
        dynamic_world->stepSimulation(delta_time, 10);

    for (const auto& [id, body] : m_bodies) {
        if (!body || body->m_massa <= 0.f)
            continue;
        const auto entity_transform = registry->get<transform>(id);
        if (!entity_transform)
            continue;

        btTransform world_transform;
        body->m_rigid_body->getMotionState()->getWorldTransform(world_transform);
        const auto origin = world_transform.getOrigin();
        entity_transform->set_world_position(
            {origin.getX(), origin.getY(), origin.getZ()}
        );

        if (body->type != physics::body_type::character) {
            auto orientation = world_transform.getRotation();
            if (entity_transform->parent_id) {
                if (auto parent = registry->get<transform>(entity_transform->parent_id)) {
                    glm::vec3 scale(1), position(0), skew(0); glm::vec4 perspective(0); glm::quat parent_rotation(1,0,0,0);
                    glm::decompose(parent->get_world_model_matrix(), scale, parent_rotation, position, skew, perspective);
                    const auto local = glm::inverse(parent_rotation) * glm::quat(
                        orientation.w(), orientation.x(), orientation.y(), orientation.z());
                    orientation = btQuaternion(local.x, local.y, local.z, local.w);
                }
            }
            float x, y, z;
            glm::extractEulerAngleXYZ(glm::mat4_cast(glm::quat(
                orientation.w(), orientation.x(), orientation.y(), orientation.z()
            )), x, y, z);
            entity_transform->set_rotation({
                x * radians_to_degrees,
                y * radians_to_degrees,
                z * radians_to_degrees
            });
        }
    }
}

bool physics_system::remove(btRigidBody*& body) {
    if (!dynamic_world || !body)
        return false;

    const int index = dynamic_world->getCollisionObjectArray().findLinearSearch(body);
    if (index == dynamic_world->getNumCollisionObjects())
        return false;

    dynamic_world->removeRigidBody(body);
    body = nullptr;
    return true;
}

btDiscreteDynamicsWorld* physics_system::world() {
    return dynamic_world;
}

void physics_system::set_gravity(const bgui::vec3& gravity_value) {
    m_gravity = gravity_value;
    if (dynamic_world) {
        dynamic_world->setGravity(btVector3(
            m_gravity.x,
            m_gravity.y,
            m_gravity.z
        ));
    }
}

const bgui::vec3& physics_system::gravity() const noexcept {
    return m_gravity;
}

ray_result physics_system::cast_ray(const ray& ray_value) {
    if (!dynamic_world)
        return {false, bgui::vec3(0.f), bgui::vec3(0.f), nullptr};

    const btVector3 origin(
        ray_value.origem.x,
        ray_value.origem.y,
        ray_value.origem.z
    );
    const btVector3 direction(
        ray_value.direction.x,
        ray_value.direction.y,
        ray_value.direction.z
    );
    const btVector3 destination = origin + direction * 300.f;
    btCollisionWorld::ClosestRayResultCallback callback(origin, destination);
    dynamic_world->rayTest(origin, destination, callback);

    ray_result result{false, bgui::vec3(0.f), bgui::vec3(0.f), nullptr};
    if (!callback.hasHit())
        return result;

    result.pontoDeColisao = {
        callback.m_hitPointWorld.getX(),
        callback.m_hitPointWorld.getY(),
        callback.m_hitPointWorld.getZ()
    };
    const auto normal = callback.m_hitNormalWorld.normalized();
    result.normalAtingida = {
        normal.getX(),
        normal.getY(),
        normal.getZ()
    };
    result.objetoAtingido = callback.m_collisionObject;
    result.atingiu = true;
    return result;
}

}
