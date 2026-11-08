#pragma once
#include "component.hpp"
#include <utils/vec.hpp>

namespace commons {
// Plans a bounded XZ route. Movement remains under the script/physics controller.
struct pathfinder : component {
    static constexpr component::mask mask = COMPONENTE_PATHFINDER;
    float cell_size{0.5f}, agent_radius{0.25f};
    int search_radius{32};
    bool configure(float cell, float radius, int extent);
    bool find_path(const bgui::vec3& destination);
    void clear() { m_path.clear(); }
    std::vector<bgui::vec3> path() const;
    bool analyze(const rapidjson::Value&) override;
    bool serialize(rapidjson::Value&, rapidjson::Document::AllocatorType&) const override;
private:
    std::vector<bgui::vec3> m_path;
};
}
