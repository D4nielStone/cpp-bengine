#include "profiling/profiler.hpp"
#include <algorithm>
#include <array>
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
    std::vector<transform> result(m_instances.size());
    for (std::size_t i=0;i<result.size();++i) {
        result[i]=m_instances[i];
        result[i].reg=m_instances[i].reg;
        result[i].my_object=m_instances[i].my_object;
        result[i].m_usar_target=m_instances[i].m_usar_target;
    }
    return result;
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
    std::vector<transform> copied(i.size());
    for (std::size_t index=0;index<i.size();++index) {
        copied[index]=i[index];
        copied[index].reg=i[index].reg;
        copied[index].my_object=i[index].my_object;
        copied[index].m_usar_target=i[index].m_usar_target;
    }
    m_instances=std::move(copied);
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
    draw(shader, nullptr);
}

void mesh::draw(shader& shader, const glm::mat4* instance_parent) {
    material_binding binding(*m_material, shader);
    draw_instances(shader, instance_parent);
}

void mesh::draw_instances(shader& shader, const glm::mat4* instance_parent) const {
    shader.setBool("instance", !m_instances.empty());
    if (m_instances.empty()) {
        draw_geometry();
        return;
    }
    constexpr std::size_t maximum_batch = 128;
    std::array<glm::mat4, maximum_batch> matrices;
    const auto location = shader.uniform_location("transformacoes[0]");
    std::size_t batch_size = maximum_batch;
    if (location >= 0 && shader.uniform_location("transformacoes[127]") < 0) {
        const char* const name = "transformacoes[0]";
        GLuint index = GL_INVALID_INDEX;
        glGetUniformIndices(shader.ID, 1, &name, &index);
        GLint size = 1;
        if (index != GL_INVALID_INDEX)
            glGetActiveUniformsiv(shader.ID, 1, &index, GL_UNIFORM_SIZE, &size);
        batch_size = std::clamp<std::size_t>(size, 1, maximum_batch);
    }
    for (std::size_t begin = 0; begin < m_instances.size(); begin += batch_size) {
        const auto count = std::min(batch_size, m_instances.size() - begin);
        if (location >= 0) {
            for (std::size_t i = 0; i < count; ++i) {
                matrices[i] = m_instances[begin + i].get_world_model_matrix();
                if (instance_parent) matrices[i] = *instance_parent * matrices[i];
            }
            glUniformMatrix4fv(location, count, GL_FALSE, glm::value_ptr(matrices.front()));
        }
        draw_geometry(count);
    }
}

void mesh::draw_geometry(std::size_t instance_count) const {
    GLint depth_function = GL_LESS;
    if (m_overlayed) {
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glDepthFunc(GL_ALWAYS);
    }

    const auto submitted_instances = m_instances.empty() ? 1 :
        (instance_count ? instance_count : std::min<std::size_t>(128, m_instances.size()));
    Bubble::profiler::instance().draw(m_indices.size() * submitted_instances,
        (m_indices.size() / 3) * submitted_instances);
    glBindVertexArray(m_VAO);
    if (m_instances.empty()) {
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawElementsInstanced(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, nullptr, instance_count ? instance_count : std::min<std::size_t>(128, m_instances.size()));
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
