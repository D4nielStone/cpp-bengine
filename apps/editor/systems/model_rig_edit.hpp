#pragma once
#include "loaders/editable_model.hpp"
#include "loaders/model_pose.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace editor::ui {
inline std::map<std::string, aiMatrix4x4> rig_rest_globals(const commons::model_rig& rig) {
    std::map<std::string, aiMatrix4x4> result;
    const auto resolve = [&](const auto& self, const commons::rig_joint& joint) -> aiMatrix4x4 {
        if (const auto found = result.find(joint.id); found != result.end()) return found->second;
        auto parent = aiMatrix4x4{};
        if (!joint.parent.empty()) {
            const auto found = std::find_if(rig.joints.begin(), rig.joints.end(), [&](const auto& value) { return value.id == joint.parent; });
            if (found != rig.joints.end()) parent = self(self, *found);
        }
        return result[joint.id] = parent * commons::model_pose::matrix(joint.rest);
    };
    std::string error;
    if (!rig.validate(error)) return result;
    for (const auto& joint : rig.joints) resolve(resolve, joint);
    return result;
}
inline bool bind_model_rig(commons::editable_model_data& data, std::string& error) {
    if (!data.validate(error) || data.rig.joints.empty()) { error = "Crie os ossos antes de vincular a malha."; return false; }
    const auto globals = rig_rest_globals(data.rig);
    auto next = data;
    for (auto& mesh : next.meshes) {
        mesh.skin.clear(); mesh.skin.reserve(mesh.vertices.size());
        for (const auto& vertex : mesh.vertices) {
            const aiVector3D point{vertex.position.x,vertex.position.y,vertex.position.z};
            std::vector<std::pair<double,std::string>> distances;
            distances.reserve(data.rig.joints.size());
            for (const auto& joint : data.rig.joints) {
                const auto tip = globals.at(joint.id) * aiVector3D{};
                const auto origin = joint.parent.empty() ? tip : globals.at(joint.parent) * aiVector3D{};
                const auto segment = tip - origin;
                const auto length = segment.SquareLength();
                const auto t = length > 1.e-12f ? std::clamp(((point - origin) * segment) / length, 0.f, 1.f) : 0.f;
                const auto distance = (point - (origin + segment * t)).SquareLength();
                distances.emplace_back(1. / std::max(double(distance), 1.e-8), joint.id);
            }
            const auto count = std::min<std::size_t>(4, distances.size());
            std::partial_sort(distances.begin(), distances.begin() + count, distances.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
            double sum = 0; for (std::size_t i = 0; i < count; ++i) sum += distances[i].first;
            auto& weights = mesh.skin.emplace_back();
            for (std::size_t i = 0; i < count; ++i) weights.push_back({distances[i].second, float(distances[i].first / sum)});
        }
    }
    if (!next.validate(error)) return false;
    data = std::move(next);
    return true;
}
inline float rig_vertex_weight(const std::vector<commons::rig_weight>& weights, const std::string& joint) {
    const auto found = std::find_if(weights.begin(), weights.end(), [&](const auto& weight) { return weight.joint == joint; });
    return found == weights.end() ? 0.f : found->weight;
}
inline bool set_rig_vertex_weights(commons::editable_model_data& data, std::size_t mesh,
    const std::map<std::size_t, float>& values, const std::string& joint, std::string& error) {
    if (mesh >= data.meshes.size() || values.empty() ||
        std::none_of(data.rig.joints.begin(), data.rig.joints.end(), [&](const auto& bone) { return bone.id == joint; })) {
        error = "Selecione vértices e um osso, e informe um peso entre zero e um."; return false;
    }
    auto next = data;
    auto& geometry = next.meshes[mesh];
    geometry.skin.resize(geometry.vertices.size());
    for (const auto& [vertex, value] : values) {
        if (!std::isfinite(value) || value < 0 || value > 1 || vertex >= geometry.vertices.size()) {
            error = "Selecione vértices e um osso, e informe um peso entre zero e um."; return false;
        }
        auto& weights = geometry.skin[vertex];
        std::erase_if(weights, [&](const auto& weight) { return weight.joint == joint; });
        // Reserve an influence for the painted joint within the native format's limit.
        if (value > 0 && weights.size() >= 8) {
            std::stable_sort(weights.begin(), weights.end(), [](const auto& a, const auto& b) { return a.weight > b.weight; });
            weights.resize(7);
        }
        float sum = 0; for (const auto& weight : weights) sum += weight.weight;
        if (sum > 0) for (auto& weight : weights) weight.weight = weight.weight / sum * (1 - value);
        std::erase_if(weights, [](const auto& weight) { return weight.weight <= 0; });
        if (value > 0) weights.push_back({joint, sum > 0 ? value : 1.f});
    }
    if (!next.validate(error)) return false;
    data = std::move(next);
    return true;
}
inline bool set_rig_vertex_weight(commons::editable_model_data& data, std::size_t mesh,
    const std::set<std::size_t>& vertices, const std::string& joint, float value, std::string& error) {
    std::map<std::size_t, float> values;
    for (const auto vertex : vertices) values.emplace(vertex, value);
    return set_rig_vertex_weights(data, mesh, values, joint, error);
}

inline float rig_brush_influence(bgui::vec2 point, bgui::vec2 start, bgui::vec2 end, float radius, float strength) {
    if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(strength) || strength < 0 || strength > 1) return 0;
    const auto segment = end - start;
    const float length = segment.x * segment.x + segment.y * segment.y;
    const auto offset = point - start;
    const float t = length > 0 ? std::clamp((offset.x * segment.x + offset.y * segment.y) / length, 0.f, 1.f) : 0;
    const auto distance = point - (start + segment * t);
    const float fraction = std::sqrt(distance.x * distance.x + distance.y * distance.y) / radius;
    if (!std::isfinite(fraction) || fraction >= 1) return 0;
    const float falloff = 1 - fraction;
    return strength * falloff * falloff * (3 - 2 * falloff);
}
}
