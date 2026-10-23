#pragma once
#include "system.hpp"
#include <iostream>
#include "utils/vec.hpp"
#include "util/ray.hpp"
#include <bullet/btBulletDynamicsCommon.h>
#include "components/physics.hpp"

#include <chrono>
#include <unordered_map>

class MyContactCallback : public btCollisionWorld::ContactResultCallback {
public:
    int colisoes = 0;
    btScalar addSingleResult 	( 	btManifoldPoint &  	cp,
		const btCollisionObjectWrapper *  	colObj0Wrap,
		int  	partId0,
		int  	index0,
		const btCollisionObjectWrapper *  	colObj1Wrap,
		int  	partId1,
		int  	index1
	) 	 override
    {
        colisoes ++;
        return 0;
    }
};

namespace COMMONS_NS {
    struct physics_system : system {
         physics_system();
        ~physics_system() override;

        void update(const std::shared_ptr<ecs>&) override;
        void setup(const std::shared_ptr<ecs>&) override;
        bool remove(btRigidBody*& body);
        btDiscreteDynamicsWorld* world();
        void set_gravity(const bgui::vec3& gravity);
        const bgui::vec3& gravity() const noexcept;
        float speed;
        ray_result cast_ray(const ray& ray);
    private:
        void ensure_world();
        void synchronize_entities(const std::shared_ptr<ecs>& registry);

        btDefaultCollisionConfiguration* configColisao{nullptr};
        btCollisionDispatcher* expedidor{nullptr};
        btBroadphaseInterface* faseAmpla{nullptr};
        btSequentialImpulseConstraintSolver* solucionador{nullptr};
        btDiscreteDynamicsWorld* dynamic_world{nullptr};
        bgui::vec3 m_gravity{0.f, -9.8f, 0.f};
        std::unordered_map<std::uint32_t, std::shared_ptr<physics>> m_bodies;
        std::chrono::steady_clock::time_point m_last_update{};
    };
}
