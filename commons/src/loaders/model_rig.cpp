#include "loaders/model_rig.hpp"
#include "loaders/model_pose.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace commons {
bool rig_transform::valid() const {
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(position[axis]) || std::abs(position[axis]) > 1.e6f ||
            !std::isfinite(rotation[axis]) || std::abs(rotation[axis]) > 360000.f ||
            !std::isfinite(scale[axis]) || scale[axis] < .0001f || scale[axis] > 10000.f)
            return false;
    return true;
}

bool model_rig::validate(std::string& error) const {
    if (joints.size() > 512) { error = "O rig excede o limite de 512 ossos."; return false; }
    std::map<std::string, const rig_joint*> ids;
    for (const auto& joint : joints) {
        if (joint.id.empty() || joint.id.size() > 128 || joint.name.empty() || joint.name.size() > 256 ||
            joint.name.find('\0') != std::string::npos || !joint.rest.valid() ||
            !std::all_of(joint.id.begin(), joint.id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            }) || !ids.emplace(joint.id, &joint).second) {
            error = "O rig contém um osso inválido ou um identificador repetido."; return false;
        }
    }
    for (const auto& joint : joints) {
        std::set<std::string> ancestors{joint.id};
        auto parent = joint.parent;
        while (!parent.empty()) {
            const auto found = ids.find(parent);
            if (found == ids.end() || !ancestors.insert(parent).second) {
                error = "O rig contém um pai inexistente ou um ciclo na hierarquia."; return false;
            }
            parent = found->second->parent;
        }
    }
    std::map<std::string, aiMatrix4x4> globals;
    const auto global = [&](const auto& self, const rig_joint& joint) -> aiMatrix4x4 {
        if (const auto found = globals.find(joint.id); found != globals.end()) return found->second;
        const auto parent = joint.parent.empty() ? aiMatrix4x4{} : self(self, *ids.at(joint.parent));
        return globals[joint.id] = parent * model_pose::matrix(joint.rest);
    };
    for (const auto& joint : joints) {
        const auto transform = global(global, joint);
        const auto determinant = transform.Determinant();
        if (!std::isfinite(determinant) || determinant == 0) {
            error = "A hierarquia produz uma transformação de osso inválida."; return false;
        }
        auto inverse = transform; inverse.Inverse();
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            if (!std::isfinite(transform[row][column]) || !std::isfinite(inverse[row][column])) {
                error = "A hierarquia excede a precisão suportada para o rig."; return false;
            }
    }
    return true;
}
}
