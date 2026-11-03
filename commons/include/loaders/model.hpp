    #pragma once
#include <assimp/scene.h>
#include <filesystem>
#include "commons_namespace.hpp"
#include "util/mesh.hpp"
#include <iostream>
#include "shader.hpp"
#include "model_pose.hpp"
#include <chrono>
#include <unordered_map>

namespace COMMONS_NS {
    class model {
    public:
        model() = default;
        // CPU-only import, safe on a worker. No OpenGL or shared GUI asset cache.
        static std::unique_ptr<model> prepare(const std::string& path);
        static std::unique_ptr<model> prepare(const std::string& path, const model_import_options* options);
        model(const char* directory);
        model(const std::string& directory);

        // Independent CPU geometry and animation state; GPU buffers upload on first draw.
        std::unique_ptr<model> clone() const;

        mesh& getMalha(size_t i);
        shader& getShader();
        void setShader(const shader&);
        void draw();
        void set_animation(unsigned clip);
        void update_animation(double seconds);
        void load_model(const std::string& path);
        std::string getDiretorio() const;
        const std::string& get_source_path() const noexcept { return source_path; }
        bool source_available() const;
        void relocate_source_path(const std::filesystem::path& path) {
            source_path = path.string();
            directory = path.parent_path().string();
            m_source_missing = false;
        }
        std::vector<mesh> meshes;

        void process_node(aiNode* node, const aiScene* scene);
        mesh process_mesh(aiMesh* mesh, const aiScene* scene);
        texture load_texture(aiMaterial*, const aiTextureType&);
        static bool hasTextura(aiMaterial*, const aiTextureType&);
    private:
        using embedded_textures = std::unordered_map<std::string, texture>;
        mesh process_mesh(aiMesh*, const aiScene*, embedded_textures&);
        texture load_texture(aiMaterial*, const aiTextureType&, embedded_textures&);
        explicit model(shader::deferred_t deferred) : m_shader(deferred) {}
    protected:
        void load_model_data(const std::string& path, bool upload = true, const model_import_options* options = nullptr);
        std::shared_ptr<model_pose> m_pose;
        unsigned m_animation_clip = 0;
        std::chrono::steady_clock::time_point m_animation_start;
        std::string directory;
        std::string source_path;
        mutable bool m_source_missing{false};
        shader m_shader;
    };
}
