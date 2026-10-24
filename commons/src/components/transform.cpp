#include "components/transform.hpp"
#include "core/ecs.hpp"

#include <cmath>
#include <unordered_set>

using namespace COMMONS_NS;

glm::vec3 to_glm(const bgui::vec3& v) {
    return glm::vec3(v.x, v.y, v.z);
}

transform::transform(const bgui::vec3& p, const bgui::vec3& r, const bgui::vec3& e) :
			position(p), up({0.f, 1.f, 0.f}),
			rotation(r), target(new bgui::vec3(1.f)), m_target_novo(true),
			scale(e), m_usar_target(false) {}

transform::~transform() {
    if(m_target_novo) delete target;
}

transform& transform::operator=(const transform& tr) {
	this->position = tr.get_position();
	this->rotation = tr.get_rotation();
	this->scale = tr.get_scale();
	this->parent_id = tr.parent_id;
	*this->target = tr.get_target();
	this->up = tr.get_up();
	return *this;
}
bool transform::analyze(const rapidjson::Value& value) {
	if(value.HasMember("position"))
    {
        auto pos = value["position"].GetArray();
		position = { pos[0].GetFloat(), pos[1].GetFloat(), pos[2].GetFloat() };
    } else return false;
    if(value.HasMember("rotation"))
    {
        auto rot = value["rotation"].GetArray();
        rotation = { rot[0].GetFloat(), rot[1].GetFloat(), rot[2].GetFloat() };
    } else return false;
    if(value.HasMember("scale"))
    {
    auto esc = value["scale"].GetArray();
    scale = { esc[0].GetFloat(), esc[1].GetFloat(), esc[2].GetFloat() };
    } else return false;
    return true;
};

bool transform::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const {
    // posição Vetor 3
    rapidjson::Value pos(rapidjson::kArrayType);
    pos.PushBack(position.x, allocator);
    pos.PushBack(position.y, allocator);
    pos.PushBack(position.z, allocator);
    value.AddMember("position", pos, allocator);

    // rotação Vetor 3
    rapidjson::Value rot(rapidjson::kArrayType);
    rot.PushBack(rotation.x, allocator);
    rot.PushBack(rotation.y, allocator);
    rot.PushBack(rotation.z, allocator);
    value.AddMember("rotation", rot, allocator);

    // scale Vetor 3
    rapidjson::Value esc(rapidjson::kArrayType);
    esc.PushBack(scale.x, allocator);
    esc.PushBack(scale.y, allocator);
    esc.PushBack(scale.z, allocator);
    value.AddMember("scale", esc, allocator);
    return true;
}

glm::mat4 transform::get_model_matrix() {
    matrizmodelo = get_world_model_matrix();
    return matrizmodelo;
}

namespace {
glm::mat4 local_matrix(const transform& value) {
    auto result = glm::mat4(1.f);
    result = glm::translate(result, to_glm(value.position));
    result = glm::rotate(result, glm::radians(value.rotation.x), glm::vec3(1.f, 0.f, 0.f));
    result = glm::rotate(result, glm::radians(value.rotation.y), glm::vec3(0.f, 1.f, 0.f));
    result = glm::rotate(result, glm::radians(value.rotation.z), glm::vec3(0.f, 0.f, 1.f));
    return glm::scale(result, to_glm(value.scale));
}

glm::mat4 world_matrix(
    const transform& value,
    std::unordered_set<uint32_t>& visited
) {
    const auto local = local_matrix(value);
    if (!value.parent_id || !value.reg ||
        !visited.insert(value.my_object).second)
        return local;
    const auto parent = value.reg->get<transform>(value.parent_id);
    if (!parent)
        return local;
    return world_matrix(*parent, visited) * local;
}

bool decompose_transform(
    const glm::mat4& matrix,
    bgui::vec3& position,
    bgui::vec3& rotation,
    bgui::vec3& scale
) {
    glm::vec3 translation{};
    glm::vec3 decomposed_scale{};
    glm::vec3 skew{};
    glm::vec4 perspective{};
    glm::quat orientation{};
    if (!glm::decompose(matrix, decomposed_scale, orientation, translation, skew, perspective))
        return false;
    position = {translation.x, translation.y, translation.z};
    scale = {decomposed_scale.x, decomposed_scale.y, decomposed_scale.z};
    const auto euler = glm::degrees(glm::eulerAngles(orientation));
    rotation = {euler.x, euler.y, euler.z};
    return true;
}

glm::quat rotation_quaternion(const bgui::vec3& euler) {
    return glm::angleAxis(glm::radians(euler.x), glm::vec3(1.f, 0.f, 0.f)) *
        glm::angleAxis(glm::radians(euler.y), glm::vec3(0.f, 1.f, 0.f)) *
        glm::angleAxis(glm::radians(euler.z), glm::vec3(0.f, 0.f, 1.f));
}
}

glm::mat4 transform::get_world_model_matrix() const {
    std::unordered_set<uint32_t> visited;
    return world_matrix(*this, visited);
}

bgui::vec3 transform::get_position() const {
    return position;
}

bgui::vec3 transform::get_world_position() const {
    const auto matrix = get_world_model_matrix();
    return {matrix[3][0], matrix[3][1], matrix[3][2]};
}

bgui::vec3 transform::get_scale() const {
    return scale;
}

bgui::vec3 transform::get_rotation() const {
    return rotation;
}

bgui::vec3 transform::get_world_rotation() const {
    bgui::vec3 world_position{}, world_rotation{}, world_scale{};
    if (!decompose_transform(
            get_world_model_matrix(), world_position, world_rotation, world_scale))
        return rotation;
    return world_rotation;
}

bgui::vec3 transform::get_target() const {
    return *target;
}

bgui::vec3 transform::get_up() const {
    return up;
}

bool transform::is_using_target() const {
    return m_usar_target;
}

void transform::set_model_matrix(const glm::mat4& m) {
    matrizmodelo = m;
}

void transform::set_up(const bgui::vec3& c) {
    up = c;
}

void  transform::set_position(const bgui::vec3& v) {
    position = v;
}

void transform::set_world_position(const bgui::vec3& value) {
    if (!parent_id || !reg) {
        position = value;
        return;
    }
    const auto parent = reg->get<transform>(parent_id);
    if (!parent) {
        position = value;
        return;
    }
    const auto parent_matrix = parent->get_world_model_matrix();
    if (std::abs(glm::determinant(parent_matrix)) < 1e-8f)
        return;
    const auto local = glm::inverse(parent_matrix) *
        glm::vec4(value.x, value.y, value.z, 1.f);
    position = {local.x, local.y, local.z};
}

void  transform::set_scale(const bgui::vec3& v) {
    scale = v;
}

void  transform::set_rotation(const bgui::vec3& v) {
    m_usar_target = false;
    rotation = v;
}

void transform::set_world_rotation(const bgui::vec3& value) {
    m_usar_target = false;
    if (!parent_id || !reg) {
        rotation = value;
        return;
    }
    const auto parent = reg->get<transform>(parent_id);
    if (!parent) {
        rotation = value;
        return;
    }
    const auto parent_rotation = parent->get_world_rotation();
    const auto local_rotation = glm::inverse(rotation_quaternion(parent_rotation)) *
        rotation_quaternion(value);
    const auto euler = glm::degrees(glm::eulerAngles(local_rotation));
    rotation = {euler.x, euler.y, euler.z};
}

bool transform::set_parent(const uint32_t new_parent, const bool preserve_world) {
    if (new_parent == parent_id)
        return true;
    if (new_parent == my_object)
        return false;

    std::shared_ptr<transform> parent_transform;
    if (new_parent) {
        if (!reg)
            return false;
        parent_transform = reg->get<transform>(new_parent);
        if (!parent_transform)
            return false;

        auto ancestor = parent_transform;
        std::unordered_set<uint32_t> visited;
        while (ancestor) {
            if (ancestor->my_object == my_object ||
                !visited.insert(ancestor->my_object).second)
                return false;
            ancestor = ancestor->parent_id
                ? reg->get<transform>(ancestor->parent_id)
                : nullptr;
        }
    }

    bgui::vec3 next_position = position;
    bgui::vec3 next_rotation = rotation;
    bgui::vec3 next_scale = scale;
    if (preserve_world) {
        const auto world = get_world_model_matrix();
        auto local = world;
        if (parent_transform) {
            const auto parent_world = parent_transform->get_world_model_matrix();
            if (std::abs(glm::determinant(parent_world)) < 1e-8f)
                return false;
            local = glm::inverse(parent_world) * world;
        }
        if (!decompose_transform(local, next_position, next_rotation, next_scale))
            return false;
    }

    parent_id = new_parent;
    if (preserve_world) {
        position = next_position;
        rotation = next_rotation;
        scale = next_scale;
    }
    return true;
}

void  transform::move(const bgui::vec3& v) {
    position = position + v;
}

void  transform::apply_scale(const bgui::vec3& v) {
    scale = scale + v;
}

void  transform::rotate(const bgui::vec3& v) {
    m_usar_target = false;
    rotation = rotation + v;
}

void  transform::look_at_vector(const bgui::vec3& pos) {
    m_usar_target = true;
    if(!m_target_novo)
        target = new bgui::vec3(1.f);
    m_target_novo = true;
    *target = pos;
}
