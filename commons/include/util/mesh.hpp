#pragma once
#include <vector>
#include <utility>
#include <memory>
#include <glm/glm.hpp>
#include "commons_namespace.hpp"
#include "material.hpp"
#include "vertex.hpp"
#include "components/transform.hpp"
#include "loaders/shader.hpp"

namespace COMMONS_NS {
    /**
     * \class mesh
     * \brief Gerencia os vertices, indices, material e ect.
     * Usada para renderizar objects 3D.
     */
     class mesh {
     public:
        mesh() = default;
        mesh(const std::vector<vertex>& vertices, const std::vector<unsigned int> &indices, const material& material);
        std::vector<vertex>         getVertices() const;
        const std::vector<vertex>& vertices() const noexcept { return m_vertices; }
        std::vector<transform>      getInstances() const;
        const std::vector<transform>& instances() const noexcept { return m_instances; }
        std::vector<unsigned int>   getIndices() const;
        const std::vector<unsigned int>& indices() const noexcept { return m_indices; }
        material                    getMaterial() const;
        const material& material_configuration() const noexcept { return *m_material; }
        void                        share_material_from(const mesh& source) noexcept { m_material = source.m_material; }
        unsigned int                getVertexArray() const;
        unsigned int                getElementBuffer() const;
        unsigned int                getVertexBuffer() const;
        void                        set_vertices(const std::vector<vertex>&);
        void                        setInstances(const std::vector<transform>&);
        void                        setInstances(std::vector<transform>&& instances) { m_instances = std::move(instances); }
        void                        set_indices(const std::vector<unsigned int>&);
        void                        setMaterial(const material&);
        void                        setSobreposition(const bool);
        void                        unload();
        void                        load();
        void                        draw(shader& shader);
        bool                        isLoaded() const;
        bool                        isOverlayed() const;
        ~mesh();
     private:
        friend class render_system;
        void draw_geometry() const;
        bool                        m_overlayed { false },
                                    m_loaded { false };
        std::vector<transform>      m_instances;
        std::vector<vertex>         m_vertices;
        std::vector<unsigned int>   m_indices;
        std::shared_ptr<const material> m_material = std::make_shared<material>();
        unsigned int                m_VAO,
                                    m_VBO,
                                    m_EBO;

    };
} // namespace commons
