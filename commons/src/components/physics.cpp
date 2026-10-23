#include "components/physics.hpp"

#include <rapidjson/document.h>

#include <stdexcept>
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

    btVector3 inertia(0.f, 0.f, 0.f);
    if (m_massa > 0.f)
        m_forma->calculateLocalInertia(m_massa, inertia);

    m_estado_de_movimento = new btDefaultMotionState(
        btTransform(btQuaternion::getIdentity(), btVector3(0.f, 0.f, 0.f))
    );
    btRigidBody::btRigidBodyConstructionInfo construction(
        m_massa,
        m_estado_de_movimento,
        m_forma,
        inertia
    );
    m_rigid_body = new btRigidBody(construction);
    setFriccao(friction);
    setRestituicao(restitution);
}

physics::~physics() {
    delete m_rigid_body;
    delete m_estado_de_movimento;
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
    m_massa = mass > 0.f ? mass : 0.f;
    btVector3 inertia(0.f, 0.f, 0.f);
    if (m_massa > 0.f)
        m_forma->calculateLocalInertia(m_massa, inertia);
    m_rigid_body->setMassProps(m_massa, inertia);
    m_rigid_body->updateInertiaTensor();
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
    value.AddMember("mass", m_massa, allocator);
    value.AddMember("friction", friction, allocator);
    value.AddMember("restitution", restitution, allocator);
    return true;
}
