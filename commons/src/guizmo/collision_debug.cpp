#include "guizmo/collision_debug.hpp"
#include "components/physics.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"

namespace editor {
namespace {
struct line_collector final : btIDebugDraw {
    std::vector<collision_line> lines;
    void drawLine(const btVector3& from, const btVector3& to, const btVector3& color) override {
        lines.push_back({{from.x(),from.y(),from.z()}, {to.x(),to.y(),to.z()}, {color.x(),color.y(),color.z()}});
    }
    void drawContactPoint(const btVector3&, const btVector3&, btScalar, int, const btVector3&) override {}
    void reportErrorWarning(const char*) override {}
    void draw3dText(const btVector3&, const char*) override {}
    void setDebugMode(int) override {}
    int getDebugMode() const override { return DBG_DrawWireframe; }
};
}
std::vector<collision_line> collision_lines(const std::shared_ptr<commons::ecs>& registry) {
    if (!registry) return {};
    btDefaultCollisionConfiguration configuration;
    btCollisionDispatcher dispatcher(&configuration);
    btDbvtBroadphase broadphase;
    btCollisionWorld world(&dispatcher, &broadphase, &configuration);
    line_collector collector;
    world.setDebugDrawer(&collector);
    registry->cada<commons::physics,commons::transform>([&](uint32_t id) {
        auto body = registry->get<commons::physics>(id);
        auto transform = registry->get<commons::transform>(id);
        auto visual = registry->get<commons::renderer>(id);
        const auto pose = body->synchronize_transform(*transform, visual ? visual->m_modelo : nullptr);
        const auto color = !body->collision_enabled ? btVector3(0.5f,0.5f,0.5f) :
            body->trigger ? btVector3(0.2f,0.7f,1.f) : btVector3(0.2f,1.f,0.3f);
        world.debugDrawObject(pose, body->collision_shape, color);
    });
    return std::move(collector.lines);
}
}
