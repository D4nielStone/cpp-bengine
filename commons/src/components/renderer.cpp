#include "components/renderer.hpp"

#include <cmath>
#include <string_view>
#include <utility>
#include <limits>
#include <set>

namespace commons {
namespace {
struct joint_channel { std::string id; int member{}, axis{}; };
std::optional<joint_channel> parse_channel(const std::string& path) {
    if (!path.starts_with("rig[")) return {};
    const auto close = path.find(']', 4);
    if (close == std::string::npos || close == 4) return {};
    const std::string_view property(path.data() + close + 1, path.size() - close - 1);
    int member = -1;
    if (property.starts_with(".position.")) member = 0;
    else if (property.starts_with(".rotation.")) member = 1;
    else if (property.starts_with(".scale.")) member = 2;
    if (member < 0 || property.size() != (member == 2 ? 8u : 11u)) return {};
    const int axis = property.back() == 'x' ? 0 : property.back() == 'y' ? 1 : property.back() == 'z' ? 2 : -1;
    if (axis < 0) return {};
    return joint_channel{path.substr(4, close - 4), member, axis};
}
bgui::vec3& member(rig_transform& value, int index) {
    return index == 0 ? value.position : index == 1 ? value.rotation : value.scale;
}
}
bool renderer::analyze(const rapidjson::Value& value) {
    if (!value.IsObject() || !value.HasMember("model") || !value["model"].IsString()) return false;
    auto staged = std::make_unique<model>(std::string(value["model"].GetString(), value["model"].GetStringLength()));
    renderer replacement(staged.release());
    if (!replacement.analyze_rig_pose(value)) return false;
    std::swap(m_modelo, replacement.m_modelo);
    bounds_padding = replacement.bounds_padding;
    local_instances = replacement.local_instances;
    particle_layer = replacement.particle_layer;
    if (value.HasMember("cast_shadows") && value["cast_shadows"].IsBool()) cast_shadows = value["cast_shadows"].GetBool();
    if (value.HasMember("receive_shadows") && value["receive_shadows"].IsBool()) receive_shadows = value["receive_shadows"].GetBool();
    return true;
}
std::vector<animation_property> renderer::animation_properties() const {
    std::vector<animation_property> result;
    if (!m_modelo) return result;
    result.reserve(m_modelo->rig_joints().size() * 9);
    for (const auto& joint : m_modelo->rig_joints()) {
        const auto prefix = "rig[" + joint.id + "].";
        for (const char* axis : {"x", "y", "z"}) {
            result.push_back({prefix + "position." + axis, -1.e6, 1.e6, false});
            result.push_back({prefix + "rotation." + axis, -360000, 360000, false});
            result.push_back({prefix + "scale." + axis, .0001, 10000, false});
        }
    }
    return result;
}
std::optional<double> renderer::read_animation_property(const std::string& path) const {
    const auto channel = parse_channel(path);
    if (!m_modelo || !channel) return {};
    auto pose = m_modelo->joint_pose(channel->id);
    return pose ? std::optional<double>{member(*pose, channel->member)[channel->axis]} : std::nullopt;
}
bool renderer::write_animation_property(const std::string& path, double value) {
    const auto channel = parse_channel(path);
    if (!m_modelo || !channel || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) return false;
    auto pose = m_modelo->joint_pose(channel->id);
    if (!pose) return false;
    member(*pose, channel->member)[channel->axis] = float(value);
    return m_modelo->set_joint_pose(channel->id, *pose);
}
bool renderer::analyze_rig_pose(const rapidjson::Value& value) {
    if (!analyze_instances(value)) return false;
    if (!value.HasMember("rig_pose")) return true;
    if (!m_modelo || !value["rig_pose"].IsArray() ||
        value["rig_pose"].Size() > m_modelo->rig_joints().size()) return false;
    std::vector<std::pair<std::string, rig_transform>> poses;
    std::set<std::string> ids;
    for (const auto& source : value["rig_pose"].GetArray()) {
        if (!source.IsObject() || !source.HasMember("id") || !source["id"].IsString()) return false;
        std::string id(source["id"].GetString(), source["id"].GetStringLength());
        if (!m_modelo->joint_pose(id) || !ids.insert(id).second) return false;
        rig_transform pose;
        for (auto [key, destination] : {std::pair{"position", &pose.position}, {"rotation", &pose.rotation}, {"scale", &pose.scale}}) {
            if (!source.HasMember(key) || !source[key].IsArray() || source[key].Size() != 3) return false;
            for (int axis = 0; axis < 3; ++axis) {
                if (!source[key][axis].IsNumber()) return false;
                (*destination)[axis] = source[key][axis].GetFloat();
            }
        }
        if (!pose.valid()) return false;
        poses.emplace_back(std::move(id), pose);
    }
    m_modelo->reset_pose();
    for (const auto& [id, pose] : poses) m_modelo->set_joint_pose(id, pose);
    m_modelo->apply_pose();
    return true;
}
bool renderer::serialize_rig_pose(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const {
    if (!serialize_instances(value, allocator)) return false;
    if (!m_modelo || !m_modelo->manual_pose()) return true;
    rapidjson::Value poses(rapidjson::kArrayType);
    for (const auto& joint : m_modelo->rig_joints()) {
        const auto pose = m_modelo->joint_pose(joint.id);
        if (!pose || !pose->valid()) return false;
        if (*pose == rig_transform{}) continue;
        rapidjson::Value entry(rapidjson::kObjectType);
        entry.AddMember("id", rapidjson::Value(joint.id.c_str(), allocator), allocator);
        for (const auto& [key, vector] : {std::pair{"position", pose->position}, {"rotation", pose->rotation}, {"scale", pose->scale}}) {
            rapidjson::Value components(rapidjson::kArrayType);
            for (int axis = 0; axis < 3; ++axis) components.PushBack(vector[axis], allocator);
            entry.AddMember(rapidjson::Value(key, allocator), components, allocator);
        }
        poses.PushBack(entry, allocator);
    }
    value.AddMember("rig_pose", poses, allocator);
    return true;
}
bool renderer::analyze_instances(const rapidjson::Value& value) {
    if (!value.IsObject()) return false;
    for (const auto* key : {"local_instances", "particle_layer"})
        if (value.HasMember(key) && !value[key].IsBool()) return false;
    const auto apply_settings = [&](float padding) {
        bounds_padding = padding;
        if (value.HasMember("local_instances")) local_instances = value["local_instances"].GetBool();
        if (value.HasMember("particle_layer")) particle_layer = value["particle_layer"].GetBool();
    };
    float padding=bounds_padding;
    if (value.HasMember("bounds_padding")) {
        const auto& v=value["bounds_padding"];
        if (!v.IsNumber() || !std::isfinite(v.GetDouble()) || v.GetDouble()<0 || v.GetDouble()>10000) return false;
        padding=v.GetFloat();
    }
    if (!value.HasMember("mesh_instances")) { apply_settings(padding); return true; }
    const auto& source = value["mesh_instances"];
    if (!source.IsArray() || source.Size() > 4096 ||
        (m_modelo && source.Size() != m_modelo->meshes.size())) return false;
    std::vector<std::vector<transform>> staged;
    std::size_t count = 0;
    for (const auto& mesh : source.GetArray()) {
        if (!mesh.IsArray() || (count += mesh.Size()) > 100000) return false;
        auto& instances = staged.emplace_back();
        instances.reserve(mesh.Size());
        for (const auto& record : mesh.GetArray()) {
            if (!record.IsObject()) return false;
            for (const auto* key : {"position", "rotation", "scale"}) {
                if (!record.HasMember(key) || !record[key].IsArray() || record[key].Size()!=3) return false;
                for (const auto& number : record[key].GetArray())
                    if (!number.IsNumber() || !std::isfinite(number.GetDouble()) ||
                        std::abs(number.GetDouble()) > std::numeric_limits<float>::max()) return false;
            }
            auto& pose = instances.emplace_back();
            if (!pose.analyze(record)) return false;
            // Instances use the existing mesh API's world coordinates, without ECS parents.
            if (record.HasMember("parent") || record.HasMember("rig_joint")) return false;
        }
    }
    apply_settings(padding);
    if (m_modelo)
        for (std::size_t i=0; i<staged.size(); ++i)
            m_modelo->meshes[i].setInstances(std::move(staged[i]));
    return true;
}
bool renderer::serialize_instances(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const {
    if (!value.IsObject()) return false;
    if (!m_modelo || !std::isfinite(bounds_padding) || bounds_padding<0 || bounds_padding>10000) return false;
    if (bounds_padding>0) value.AddMember("bounds_padding",bounds_padding,allocator);
    bool any = false;
    for (const auto& mesh : m_modelo->meshes) any |= !mesh.instances().empty();
    if (!any) return true;
    rapidjson::Value all(rapidjson::kArrayType);
    std::size_t count = 0;
    for (const auto& mesh : m_modelo->meshes) {
        if ((count += mesh.instances().size()) > 100000) return false;
        rapidjson::Value entries(rapidjson::kArrayType);
        for (const auto& instance : mesh.instances()) {
            if (instance.parent_id || !instance.rig_joint_id.empty() || instance.m_usar_target) return false;
            for (const auto& v : {instance.get_position(),instance.get_rotation(),instance.get_scale()})
                for (int i=0;i<3;++i) if (!std::isfinite(v[i])) return false;
            rapidjson::Value entry(rapidjson::kObjectType);
            if (!instance.serialize(entry, allocator)) return false;
            entries.PushBack(entry, allocator);
        }
        all.PushBack(entries, allocator);
    }
    value.AddMember("mesh_instances", all, allocator);
    return true;
}

}
