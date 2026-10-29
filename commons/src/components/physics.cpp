#include "components/physics.hpp"

#include <rapidjson/document.h>

#include <stdexcept>
#include <cmath>
#include <string>

using namespace COMMONS_NS;

namespace {

const char* shape_name(const physics::shapes shape) {
    switch (shape) {
    case physics::shapes::box_shape:
        return "box";
    case physics::shapes::forma_capsula:
        return "capsule";
    case physics::shapes::forma_esfera:
        return "sphere";
    case physics::shapes::forma_cilindro:
        return "cylinder";
    case physics::shapes::forma_cone:
        return "cone";
    case physics::shapes::forma_plano:
        return "plane";
    case physics::shapes::mesh_shape:
        return "mesh";
    }
    return "box";
}

}

physics::physics(const bool is_static, const shapes shape)
    : e_forma(shape),
      m_massa(is_static ? 0.f : 1.f) {
    switch (shape) {
    case shapes::box_shape:
        m_forma = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
        break;
    case shapes::forma_capsula:
        m_forma = new btCapsuleShape(0.5f, 1.8f);
        break;
    case shapes::forma_esfera:
        m_forma = new btSphereShape(0.5f);
        break;
    case shapes::forma_cilindro:
        m_forma = new btCylinderShape(btVector3(0.5f, 0.5f, 0.5f));
        break;
    case shapes::forma_cone:
        m_forma = new btConeShape(0.5f, 1.f);
        break;
    case shapes::forma_plano:
        m_forma = new btStaticPlaneShape(btVector3(0.f, 1.f, 0.f), 0.f);
        break;
    case shapes::mesh_shape:
        throw std::invalid_argument(
            "Mesh physics shapes are not supported by project files."
        );
    }

    type = is_static ? body_type::static_body : body_type::rigid;
    collision_shape = new btCompoundShape();
    btTransform local; local.setIdentity();
    collision_shape->addChildShape(local, m_forma);
    btVector3 inertia(0.f, 0.f, 0.f);
    if (m_massa > 0.f)
        collision_shape->calculateLocalInertia(m_massa, inertia);

    m_estado_de_movimento = new btDefaultMotionState(
        btTransform(btQuaternion::getIdentity(), btVector3(0.f, 0.f, 0.f))
    );
    btRigidBody::btRigidBodyConstructionInfo construction(
        m_massa,
        m_estado_de_movimento,
        collision_shape,
        inertia
    );
    m_rigid_body = new btRigidBody(construction);
    setFriccao(friction);
    setRestituicao(restitution);
}

physics::~physics() {
    delete m_rigid_body;
    delete m_estado_de_movimento;
    delete collision_shape;
    delete m_forma;
}

void physics::setModelo(model* model_value) {
    if (!model_value)
        throw std::invalid_argument("Physics model cannot be null.");
    if (e_forma == shapes::mesh_shape)
        throw std::invalid_argument(
            "Mesh physics shapes are not supported."
        );
    m_modelo = model_value;
}

void physics::setForca(const bgui::vec3& vector) {
    m_rigid_body->activate();
    m_rigid_body->applyCentralForce(
        btVector3(vector.x, vector.y, vector.z)
    );
}

void physics::setVelocidade(const bgui::vec3& speed) {
    m_rigid_body->activate();
    m_rigid_body->setLinearVelocity(
        btVector3(speed.x, speed.y, speed.z)
    );
}

void physics::setFatorLinear(const bgui::vec3& factor) {
    m_rigid_body->activate();
    m_rigid_body->setLinearFactor(
        btVector3(factor.x, factor.y, factor.z)
    );
}

void physics::setFatorAngular(const bgui::vec3& factor) {
    m_rigid_body->activate();
    m_rigid_body->setAngularFactor(
        btVector3(factor.x, factor.y, factor.z)
    );
}

void physics::setFriccao(const float factor) {
    friction = factor;
    if (m_rigid_body)
        m_rigid_body->setFriction(factor);
}

void physics::setRestituicao(const float factor) {
    restitution = factor;
    if (m_rigid_body)
        m_rigid_body->setRestitution(factor);
}

void physics::setRaioCCD(const float factor) {
    m_rigid_body->activate();
    m_rigid_body->setCcdSweptSphereRadius(factor);
}

void physics::setMass(const float mass) {
    if (!std::isfinite(mass)) return;
    m_massa = mass > 0.f ? mass : 0.f;
    if (m_massa == 0.f) type = body_type::static_body;
    else if (type == body_type::static_body) type = body_type::rigid;
    btVector3 inertia(0.f, 0.f, 0.f);
    if (m_massa > 0.f)
        collision_shape->calculateLocalInertia(m_massa, inertia);
    m_rigid_body->setMassProps(m_massa, inertia);
    m_rigid_body->updateInertiaTensor();
}

void physics::set_body_type(body_type value) {
    setMass(value == body_type::static_body ? 0.f : (m_massa > 0.f ? m_massa : 1.f));
    type = value;
    setFatorAngular(value == body_type::character ? bgui::vec3(0.f) : bgui::vec3(1.f));
    if (value == body_type::character) m_rigid_body->setAngularVelocity(btVector3(0,0,0));
    m_rigid_body->activate(true);
}

void physics::configure_collision(shapes shape, const bgui::vec3& size,
    const bgui::vec3& offset, bool enabled, bool sensor) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(size[i]) || size[i] <= 0.f || !std::isfinite(offset[i]))
            throw std::invalid_argument("Invalid collider dimensions or offset.");
    if (shape != e_forma) {
        physics replacement(false, shape);
        collision_shape->removeChildShape(m_forma);
        delete m_forma;
        m_forma = replacement.m_forma;
        replacement.collision_shape->removeChildShape(replacement.m_forma);
        replacement.m_forma = nullptr;
        btTransform local; local.setIdentity();
        collision_shape->addChildShape(local, m_forma);
        e_forma = shape;
    }
    ++collision_revision;
    collision_size = size; collision_offset = offset;
    collision_enabled = enabled; trigger = sensor;
    refresh_collision({1,1,1});
}

void physics::refresh_collision(const bgui::vec3& scale) {
    m_forma->setLocalScaling(btVector3(std::abs(scale.x)*collision_size.x,
        std::abs(scale.y)*collision_size.y, std::abs(scale.z)*collision_size.z));
    btTransform local; local.setIdentity();
    local.setOrigin(btVector3(collision_offset.x*scale.x, collision_offset.y*scale.y,
        collision_offset.z*scale.z));
    collision_shape->updateChildTransform(0, local);
    auto flags = m_rigid_body->getCollisionFlags();
    flags = (trigger || !collision_enabled) ? flags | btCollisionObject::CF_NO_CONTACT_RESPONSE
        : flags & ~btCollisionObject::CF_NO_CONTACT_RESPONSE;
    m_rigid_body->setCollisionFlags(flags);
    setMass(m_massa);
}

bgui::vec3 physics::getVelocidade() const {
    const auto velocity = m_rigid_body->getLinearVelocity();
    return {velocity.getX(), velocity.getY(), velocity.getZ()};
}

bool physics::analyze(const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;

    if (value.HasMember("mass")) {
        if (!value["mass"].IsNumber())
            return false;
        setMass(value["mass"].GetFloat());
    }
    if (value.HasMember("friction")) {
        if (!value["friction"].IsNumber())
            return false;
        setFriccao(value["friction"].GetFloat());
    }
    if (value.HasMember("restitution")) {
        if (!value["restitution"].IsNumber())
            return false;
        setRestituicao(value["restitution"].GetFloat());
    }
    auto size = collision_size, offset = collision_offset;
    for (auto entry : {std::pair{"collision_size", &size}, std::pair{"collision_offset", &offset}}) {
        if (!value.HasMember(entry.first)) continue;
        const auto& v = value[entry.first];
        if (!v.IsArray() || v.Size() != 3) return false;
        for (int i = 0; i < 3; ++i) {
            if (!v[i].IsNumber() || !std::isfinite(v[i].GetFloat())) return false;
            (*entry.second)[i] = v[i].GetFloat();
        }
    }
    for (const auto* key : {"collision_enabled", "trigger"})
        if (value.HasMember(key) && !value[key].IsBool()) return false;
    try { configure_collision(e_forma, size, offset,
        !value.HasMember("collision_enabled") || value["collision_enabled"].GetBool(),
        value.HasMember("trigger") && value["trigger"].GetBool()); }
    catch (const std::invalid_argument&) { return false; }
    if (value.HasMember("body_type")) {
        if (!value["body_type"].IsString()) return false;
        const std::string name = value["body_type"].GetString();
        if (name != "rigid" && name != "character" && name != "static") return false;
        set_body_type(name == "character" ? body_type::character :
            name == "static" ? body_type::static_body : body_type::rigid);
    }
    return true;
}

bool physics::serialize(
    rapidjson::Value& value,
    rapidjson::Document::AllocatorType& allocator
) const {
    value.SetObject();
    rapidjson::Value shape;
    const auto* name = shape_name(e_forma);
    shape.SetString(
        name,
        static_cast<rapidjson::SizeType>(std::char_traits<char>::length(name)),
        allocator
    );
    value.AddMember("shape", shape, allocator);
    value.AddMember("static", m_massa <= 0.f, allocator);
    value.AddMember("body_type", rapidjson::Value(type == body_type::character ? "character" :
        type == body_type::static_body ? "static" : "rigid", allocator), allocator);
    for (auto entry : {std::pair{"collision_size", collision_size}, std::pair{"collision_offset", collision_offset}}) {
        rapidjson::Value vector(rapidjson::kArrayType);
        for (int i = 0; i < 3; ++i) vector.PushBack(entry.second[i], allocator);
        value.AddMember(rapidjson::Value(entry.first, allocator), vector, allocator);
    }
    value.AddMember("collision_enabled", collision_enabled, allocator);
    value.AddMember("trigger", trigger, allocator);
    value.AddMember("mass", m_massa, allocator);
    value.AddMember("friction", friction, allocator);
    value.AddMember("restitution", restitution, allocator);
    return true;
}
