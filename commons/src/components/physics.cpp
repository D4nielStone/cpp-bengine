#include "components/physics.hpp"
#include "components/transform.hpp"
#include <bullet/BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <bullet/BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <algorithm>
#include <limits>

#include <rapidjson/document.h>

#include <memory>
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
        m_forma = new btConvexHullShape();
        break;
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
    delete m_triangle_mesh;
}

void physics::setModelo(model* model_value) {
    if (!model_value)
        throw std::invalid_argument("Physics model cannot be null.");
    if (e_forma == shapes::mesh_shape &&
        model_collider == model_collider_type::concave && m_massa > 0.f)
        throw std::invalid_argument(
            "Concave model colliders require a static body."
        );
    m_modelo = model_value;
    m_has_model_bounds = false;
    m_bounds_min = bgui::vec3(std::numeric_limits<float>::max());
    m_bounds_max = bgui::vec3(std::numeric_limits<float>::lowest());
    std::unique_ptr<btTriangleMesh> triangle_mesh;
    std::unique_ptr<btCollisionShape> model_shape;
    if (e_forma == shapes::mesh_shape) {
        if (model_collider == model_collider_type::concave) {
            if (m_massa > 0.f)
                throw std::invalid_argument(
                    "Concave model colliders require a static body."
                );
            triangle_mesh = std::make_unique<btTriangleMesh>(true, false);
            for (const auto& mesh : model_value->meshes) {
                const auto vertices = mesh.getVertices();
                const auto indices = mesh.getIndices();
                const auto index_at = [&vertices, &indices](std::size_t index) {
                    return indices.empty() ? index : static_cast<std::size_t>(indices[index]);
                };
                const auto triangle_count = indices.empty()
                    ? vertices.size() / 3
                    : indices.size() / 3;
                for (std::size_t triangle = 0; triangle < triangle_count; ++triangle) {
                    const auto a = index_at(triangle * 3);
                    const auto b = index_at(triangle * 3 + 1);
                    const auto c = index_at(triangle * 3 + 2);
                    if (a >= vertices.size() || b >= vertices.size() ||
                        c >= vertices.size())
                        continue;
                    const auto& p0 = vertices[a].position;
                    const auto& p1 = vertices[b].position;
                    const auto& p2 = vertices[c].position;
                    if (!std::isfinite(p0.x) || !std::isfinite(p0.y) ||
                        !std::isfinite(p0.z) || !std::isfinite(p1.x) ||
                        !std::isfinite(p1.y) || !std::isfinite(p1.z) ||
                        !std::isfinite(p2.x) || !std::isfinite(p2.y) ||
                        !std::isfinite(p2.z))
                        continue;
                    triangle_mesh->addTriangle(
                        btVector3(p0.x, p0.y, p0.z),
                        btVector3(p1.x, p1.y, p1.z),
                        btVector3(p2.x, p2.y, p2.z)
                    );
                }
            }
            model_shape = std::make_unique<btBvhTriangleMeshShape>(
                triangle_mesh.get(), true, true
            );
        } else {
            model_shape = std::make_unique<btConvexHullShape>();
        }
    }
    for (const auto& mesh : model_value->meshes)
        for (const auto& vertex : mesh.getVertices()) {
            if (!std::isfinite(vertex.position.x) ||
                !std::isfinite(vertex.position.y) ||
                !std::isfinite(vertex.position.z))
                continue;
            m_has_model_bounds = true;
            for (int axis = 0; axis < 3; ++axis) {
                m_bounds_min[axis] = std::min(m_bounds_min[axis], vertex.position[axis]);
                m_bounds_max[axis] = std::max(m_bounds_max[axis], vertex.position[axis]);
            }
            if (auto* hull = dynamic_cast<btConvexHullShape*>(model_shape.get()))
                hull->addPoint(btVector3(
                    vertex.position.x, vertex.position.y, vertex.position.z
                ), false);
        }
    if (model_shape) {
        if (auto* hull = dynamic_cast<btConvexHullShape*>(model_shape.get())) {
            hull->setMargin(0.001f);
            if (hull->getNumPoints() >= 4) {
                hull->optimizeConvexHull();
                hull->initializePolyhedralFeatures();
            }
        }
        model_shape->setLocalScaling(m_forma->getLocalScaling());
        const auto local_transform = collision_shape->getChildTransform(0);
        collision_shape->removeChildShape(m_forma);
        delete m_forma;
        delete m_triangle_mesh;
        m_forma = model_shape.release();
        m_triangle_mesh = triangle_mesh.release();
        collision_shape->addChildShape(local_transform, m_forma);
        collision_shape->recalculateLocalAabb();
        m_rigid_body->setCollisionShape(collision_shape);
        ++collision_revision;
        setMass(m_massa);
        m_rigid_body->activate(true);
    }
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
    if (mass > 0.f && e_forma == shapes::mesh_shape &&
        model_collider == model_collider_type::concave)
        throw std::invalid_argument(
            "Concave model colliders require a static body."
        );
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
    if (e_forma == shapes::mesh_shape && value != body_type::static_body &&
        model_collider == model_collider_type::concave)
        throw std::invalid_argument(
            "Concave model colliders require a static body."
        );
    setMass(value == body_type::static_body ? 0.f : (m_massa > 0.f ? m_massa : 1.f));
    type = value;
    setFatorAngular(value == body_type::character ? bgui::vec3(0.f) : bgui::vec3(1.f));
    if (value == body_type::character) m_rigid_body->setAngularVelocity(btVector3(0,0,0));
    m_rigid_body->activate(true);
}

void physics::set_model_collider_type(model_collider_type value) {
    if (value != model_collider_type::convex &&
        value != model_collider_type::concave)
        throw std::invalid_argument("Invalid model collider type.");
    if (value == model_collider_type::concave &&
        e_forma == shapes::mesh_shape && m_massa > 0.f)
        throw std::invalid_argument(
            "Concave model colliders require a static body."
        );
    if (value == model_collider)
        return;
    model_collider = value;
    if (e_forma == shapes::mesh_shape && m_modelo)
        setModelo(m_modelo);
}

void physics::configure_collision(shapes shape, const bgui::vec3& size,
    const bgui::vec3& offset, bool enabled, bool sensor) {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(size[i]) || size[i] <= 0.f || !std::isfinite(offset[i]))
            throw std::invalid_argument("Invalid collider dimensions or offset.");
    if (shape == shapes::mesh_shape &&
        model_collider == model_collider_type::concave && m_massa > 0.f)
        throw std::invalid_argument(
            "Concave model colliders require a static body."
        );
    if (shape != e_forma) {
        physics replacement(false, shape);
        collision_shape->removeChildShape(m_forma);
        delete m_forma;
        delete m_triangle_mesh;
        m_triangle_mesh = nullptr;
        m_forma = replacement.m_forma;
        replacement.collision_shape->removeChildShape(replacement.m_forma);
        replacement.m_forma = nullptr;
        btTransform local; local.setIdentity();
        collision_shape->addChildShape(local, m_forma);
        e_forma = shape;
        if (shape == shapes::mesh_shape && m_modelo)
            setModelo(m_modelo);
    }
    ++collision_revision;
    collision_size = size; collision_offset = offset;
    collision_enabled = enabled; trigger = sensor;
    refresh_collision({1,1,1});
}

void physics::refresh_collision(const bgui::vec3& entity_scale) {
    auto scale = entity_scale;
    for (int i = 0; i < 3; ++i)
        if (std::abs(scale[i]) < 0.000001f) scale[i] = 0.000001f;
    const auto previous_scale = m_forma->getLocalScaling();
    const auto previous_local = collision_shape->getChildTransform(0);
    bgui::vec3 base(1.f), center(0.f);
    if (fit_to_model && m_has_model_bounds && e_forma != shapes::mesh_shape) {
        for (int axis = 0; axis < 3; ++axis) {
            base[axis] = std::max(0.001f, m_bounds_max[axis] - m_bounds_min[axis]);
            center[axis] = (m_bounds_max[axis] + m_bounds_min[axis]) * 0.5f;
        }
        if (e_forma == shapes::forma_esfera)
            base = bgui::vec3(std::max({base.x, base.y, base.z}));
        if (e_forma == shapes::forma_capsula) {
            base.x = base.z = std::max(base.x, base.z);
            base.y = std::max(0.001f, (base.y - base.x) / 1.8f);
        }
    }
    btVector3 shape_scale(std::abs(scale.x)*base.x*collision_size.x,
        std::abs(scale.y)*base.y*collision_size.y, std::abs(scale.z)*base.z*collision_size.z);
    if (fit_to_model && m_has_model_bounds && e_forma == shapes::forma_esfera) {
        const auto radius_scale = std::max({shape_scale.x(),shape_scale.y(),shape_scale.z()});
        shape_scale = btVector3(radius_scale,radius_scale,radius_scale);
    }
    m_forma->setLocalScaling(shape_scale);
    btTransform local; local.setIdentity();
    local.setOrigin(btVector3((center.x+collision_offset.x)*scale.x,
        (center.y+collision_offset.y)*scale.y, (center.z+collision_offset.z)*scale.z));
    constexpr float radians = 0.01745329251994329577f;
    local.setRotation(btQuaternion(btVector3(1,0,0), collision_rotation.x*radians) *
        btQuaternion(btVector3(0,1,0), collision_rotation.y*radians) *
        btQuaternion(btVector3(0,0,1), collision_rotation.z*radians));
    if (previous_scale != m_forma->getLocalScaling() || !(previous_local == local)) {
        ++collision_revision;
        m_rigid_body->activate(true);
    }
    collision_shape->updateChildTransform(0, local);
    auto flags = m_rigid_body->getCollisionFlags();
    flags = (trigger || !collision_enabled) ? flags | btCollisionObject::CF_NO_CONTACT_RESPONSE
        : flags & ~btCollisionObject::CF_NO_CONTACT_RESPONSE;
    m_rigid_body->setCollisionFlags(flags);
    setMass(m_massa);
}

btTransform physics::synchronize_transform(const transform& value, model* geometry) {
    if (geometry && geometry != m_modelo) setModelo(geometry);
    const auto matrix = value.get_world_model_matrix();
    glm::vec3 scale(1.f), position(matrix[3]), skew(0.f);
    glm::vec4 perspective(0.f);
    glm::quat rotation(1.f,0.f,0.f,0.f);
    if (!glm::decompose(matrix, scale, rotation, position, skew, perspective)) {
        position = glm::vec3(matrix[3]);
        rotation = glm::quat(1.f,0.f,0.f,0.f);
        scale = {glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])),
            glm::length(glm::vec3(matrix[2]))};
    }
    refresh_collision({scale.x, scale.y, scale.z});
    btTransform result;
    result.setIdentity();
    result.setOrigin(btVector3(position.x,position.y,position.z));
    result.setRotation(btQuaternion(rotation.x,rotation.y,rotation.z,rotation.w));
    return result;
}

bgui::vec3 physics::getVelocidade() const {
    const auto velocity = m_rigid_body->getLinearVelocity();
    return {velocity.getX(), velocity.getY(), velocity.getZ()};
}

bool physics::analyze(const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;

    auto next_model_collider = model_collider;
    if (value.HasMember("model_collider")) {
        if (!value["model_collider"].IsString())
            return false;
        const std::string name = value["model_collider"].GetString();
        if (name == "convex")
            next_model_collider = model_collider_type::convex;
        else if (name == "concave")
            next_model_collider = model_collider_type::concave;
        else
            return false;
    }
    auto next_body_type = type;
    if (value.HasMember("body_type")) {
        if (!value["body_type"].IsString()) return false;
        const std::string name = value["body_type"].GetString();
        if (name != "rigid" && name != "character" && name != "static") return false;
        next_body_type = name == "character" ? body_type::character :
            name == "static" ? body_type::static_body : body_type::rigid;
    }
    if (e_forma == shapes::mesh_shape &&
        next_model_collider == model_collider_type::concave &&
        next_body_type != body_type::static_body &&
        value.HasMember("body_type"))
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
    auto size = collision_size, offset = collision_offset, rotation = collision_rotation;
    for (auto entry : {std::pair{"collision_size", &size}, std::pair{"collision_offset", &offset}, std::pair{"collision_rotation", &rotation}}) {
        if (!value.HasMember(entry.first)) continue;
        const auto& v = value[entry.first];
        if (!v.IsArray() || v.Size() != 3) return false;
        for (int i = 0; i < 3; ++i) {
            if (!v[i].IsNumber() || !std::isfinite(v[i].GetFloat())) return false;
            (*entry.second)[i] = v[i].GetFloat();
        }
    }
    for (const auto* key : {"collision_enabled", "trigger", "fit_to_model"})
        if (value.HasMember(key) && !value[key].IsBool()) return false;
    fit_to_model = value.HasMember("fit_to_model") && value["fit_to_model"].GetBool();
    collision_rotation = rotation;
    try { configure_collision(e_forma, size, offset,
        !value.HasMember("collision_enabled") || value["collision_enabled"].GetBool(),
        value.HasMember("trigger") && value["trigger"].GetBool()); }
    catch (const std::invalid_argument&) { return false; }
    try {
        if (value.HasMember("body_type"))
            set_body_type(next_body_type);
        set_model_collider_type(next_model_collider);
    } catch (const std::invalid_argument&) {
        return false;
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
    value.AddMember("model_collider", rapidjson::Value(
        model_collider == model_collider_type::concave ? "concave" : "convex",
        allocator), allocator);
    value.AddMember("static", m_massa <= 0.f, allocator);
    value.AddMember("body_type", rapidjson::Value(type == body_type::character ? "character" :
        type == body_type::static_body ? "static" : "rigid", allocator), allocator);
    for (auto entry : {std::pair{"collision_size", collision_size}, std::pair{"collision_offset", collision_offset}, std::pair{"collision_rotation", collision_rotation}}) {
        rapidjson::Value vector(rapidjson::kArrayType);
        for (int i = 0; i < 3; ++i) vector.PushBack(entry.second[i], allocator);
        value.AddMember(rapidjson::Value(entry.first, allocator), vector, allocator);
    }
    value.AddMember("fit_to_model", fit_to_model, allocator);
    value.AddMember("collision_enabled", collision_enabled, allocator);
    value.AddMember("trigger", trigger, allocator);
    value.AddMember("mass", m_massa, allocator);
    value.AddMember("friction", friction, allocator);
    value.AddMember("restitution", restitution, allocator);
    return true;
}
