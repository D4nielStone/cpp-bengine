#include "commons_namespace.hpp"
#include "glad.h"
#include "util/mesh.hpp"
#include "util/material.hpp"
#include "debugging/debug.hpp"
#include "util/vertex.hpp"
#include "systems/render_system.hpp"

using namespace COMMONS_NS;


mesh::mesh(const std::vector<vertex>& vertices, const std::vector<unsigned int>& indices, const material& material) :
    m_vertices(vertices), m_indices(indices), m_material(std::make_shared<commons::material>(material)) {
}

std::vector<vertex> mesh::getVertices() const {
    return m_vertices;
}

std::vector<transform> mesh::getInstances() const {
    return m_instances;
}

std::vector<unsigned int> mesh::getIndices() const  {
    return m_indices;
}

material mesh::getMaterial() const {
    return *m_material;
}

unsigned int mesh::getVertexArray() const {
    return m_VAO;
}

unsigned int mesh::getElementBuffer() const {
    return m_EBO;
}

unsigned int mesh::getVertexBuffer() const {
    return m_VBO;
}

void mesh::set_vertices(const std::vector<vertex>& v) {
    m_vertices = v;
    if (m_loaded) {
        GLint previous = 0;
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous);
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(vertex), m_vertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, previous);
    }
}

void mesh::setInstances(const std::vector<transform>& i) {
    m_instances = i;
}

void mesh::set_indices(const std::vector<unsigned int>& i) {
    m_indices = i;
}

void mesh::setMaterial(const material& m) {
    m_material = std::make_shared<material>(m);
}

void mesh::setSobreposition(const bool s) {
    m_overlayed = s;
}

void mesh::unload() {
    if(!m_loaded) return;
    debugging::emit(debug, "mesh", "descarregando");
    glDeleteVertexArrays(1, &m_VAO);
    glDeleteBuffers(1, &m_VBO);
    glDeleteBuffers(1, &m_EBO);
    m_loaded = false;
}

/**
 * @brief Carrega os dados do mesh para a GPU
 */
void mesh::load() {
    if(m_loaded) return;

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);

    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(vertex), m_vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int),
        m_indices.data(), GL_STATIC_DRAW);

    // vertex positions
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)0);
    // vertex normals
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, normal));
    // vertex texture coords
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, uvcoords));

    glBindVertexArray(0);
    m_loaded = true;
}

void mesh::draw(shader& shader) {
    material_binding binding(*m_material, shader);
    shader.setBool("instance", !m_instances.empty());
    for (std::size_t index = 0; index < m_instances.size(); ++index) {
        shader.setMat4("transformacoes[" + std::to_string(index) + "]",
            glm::value_ptr(m_instances[index].get_model_matrix()));
    }
    draw_geometry();
}

void mesh::draw_geometry() const {
    GLint depth_function = GL_LESS;
    if (m_overlayed) {
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glDepthFunc(GL_ALWAYS);
    }

    glBindVertexArray(m_VAO);
    if (m_instances.empty()) {
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawElementsInstanced(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, nullptr, m_instances.size());
    }
    glBindVertexArray(0);

    if (m_overlayed)
        glDepthFunc(depth_function);
}

bool mesh::isLoaded() const {
    return m_loaded;
}

bool mesh::isOverlayed() const {
    return m_overlayed;
}

mesh::~mesh() {
    unload();
}
