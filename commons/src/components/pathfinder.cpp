#include "components/pathfinder.hpp"
#include "components/physics.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace commons {
namespace {
bool finite(const bgui::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool settings(float cell, float radius, int extent) {
    return std::isfinite(cell) && cell >= .05f && cell <= 100.f &&
        std::isfinite(radius) && radius >= 0 && radius <= 100.f && extent >= 1 && extent <= 64;
}
}
bool pathfinder::configure(float cell, float radius, int extent) {
    auto owner = reg.lock();
    if (!owner || owner->get<pathfinder>(my_object).get() != this || !settings(cell,radius,extent)) return false;
    cell_size=cell; agent_radius=radius; search_radius=extent; clear(); return true;
}
std::vector<bgui::vec3> pathfinder::path() const {
    auto owner = reg.lock();
    return owner && owner->get<pathfinder>(my_object).get() == this ? m_path : std::vector<bgui::vec3>{};
}
bool pathfinder::find_path(const bgui::vec3& destination) {
    clear();
    auto owner = reg.lock();
    if (!owner || owner->get<pathfinder>(my_object).get() != this || !finite(destination) ||
        !settings(cell_size, agent_radius, search_radius)) return false;
    auto pose = owner->get<transform>(my_object);
    if (!pose) return false;
    const auto start = pose->get_world_position();
    if (!finite(start) || std::abs(destination.y - start.y) > .01f) return false;
    const int r = search_radius, width = 2*r+1, count = width*width;
    const double dx = (double(destination.x)-start.x)/cell_size;
    const double dz = (double(destination.z)-start.z)/cell_size;
    if (std::abs(dx) > r || std::abs(dz) > r) return false;
    const int gx = int(std::round(dx))+r, gz = int(std::round(dz))+r;
    struct obstacle { btVector3 low, high; };
    std::vector<obstacle> obstacles;
    owner->cada<physics, transform>([&](uint32_t id) {
        auto body = owner->get<physics>(id);
        if (id == my_object || !body->collision_enabled || body->trigger) return;
        auto visual = owner->get<renderer>(id);
        auto matrix = body->synchronize_transform(*owner->get<transform>(id), visual ? visual->m_modelo : nullptr);
        if (!body->collision_shape) return;
        if (body->e_forma == physics::shapes::forma_plano) {
            const auto normal = matrix.getBasis() * btVector3(0,1,0);
            if (std::abs(normal.y()) > .999f) return;
        }
        obstacle box;
        body->collision_shape->getAabb(matrix, box.low, box.high);
        // Ground touching the navigation plane is walkable; intersecting walls are not.
        if (box.low.y() < start.y + .001f && box.high.y() > start.y + .001f) obstacles.push_back(box);
    });
    auto point = [&](int index) { return bgui::vec3{start.x + (index%width-r)*cell_size, start.y,
        start.z + (index/width-r)*cell_size}; };
    auto segment_clear = [&](const bgui::vec3& a, const bgui::vec3& b) {
        for (const auto& box : obstacles) {
            float lo = 0, hi = 1;
            bool intersects = true;
            for (int axis : {0,2}) {
                const float av = axis == 0 ? a.x : a.z, bv = axis == 0 ? b.x : b.z;
                const float low = box.low[axis]-agent_radius, high = box.high[axis]+agent_radius;
                if (std::abs(bv-av) < 1.e-7f) { if (av < low || av > high) intersects = false; }
                else { float t0=(low-av)/(bv-av), t1=(high-av)/(bv-av); if(t0>t1) std::swap(t0,t1);
                    lo=std::max(lo,t0); hi=std::min(hi,t1); }
            }
            if (intersects && lo <= hi) return false;
        }
        return true;
    };
    const int source=r*width+r, goal=gz*width+gx;
    if (!segment_clear(start,start) || !segment_clear(point(goal),destination)) return false;
    std::vector<int> cost(count, std::numeric_limits<int>::max()), parent(count,-1);
    using entry=std::pair<int,int>;
    std::priority_queue<entry,std::vector<entry>,std::greater<entry>> open;
    auto heuristic=[&](int n) { return std::abs(n%width-gx)+std::abs(n/width-gz); };
    cost[source]=0; open.emplace(heuristic(source),source);
    while (!open.empty()) {
        auto [score,current]=open.top(); open.pop();
        if(score != cost[current]+heuristic(current)) continue;
        if(current==goal) {
            for(int n=goal; n!=-1; n=parent[n]) m_path.push_back(point(n));
            std::reverse(m_path.begin(),m_path.end());
            m_path.push_back(destination); return true;
        }
        for(auto [x,z] : {std::pair{1,0}, {-1,0}, {0,1}, {0,-1}}) {
            int nx=current%width+x, nz=current/width+z;
            if(nx<0 || nz<0 || nx>=width || nz>=width) continue;
            int next=nz*width+nx;
            if(cost[current]+1>=cost[next] || !segment_clear(point(current),point(next))) continue;
            cost[next]=cost[current]+1; parent[next]=current;
            open.emplace(cost[next]+heuristic(next),next);
        }
    }
    return false;
}
bool pathfinder::analyze(const rapidjson::Value& value) {
    if(!value.IsObject()) return false;
    float cell=cell_size, radius=agent_radius; int extent=search_radius;
    if(value.HasMember("cell_size")) { if(!value["cell_size"].IsNumber()) return false; cell=value["cell_size"].GetFloat(); }
    if(value.HasMember("agent_radius")) { if(!value["agent_radius"].IsNumber()) return false; radius=value["agent_radius"].GetFloat(); }
    if(value.HasMember("search_radius")) { if(!value["search_radius"].IsInt()) return false; extent=value["search_radius"].GetInt(); }
    if(!settings(cell,radius,extent)) return false;
    cell_size=cell; agent_radius=radius; search_radius=extent; clear(); return true;
}
bool pathfinder::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& a) const {
    if(!settings(cell_size,agent_radius,search_radius)) return false;
    value.SetObject(); value.AddMember("cell_size",cell_size,a); value.AddMember("agent_radius",agent_radius,a);
    value.AddMember("search_radius",search_radius,a); return true;
}
}
