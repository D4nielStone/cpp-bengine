/** @copyright
MIT License
Copyright (c) 2025 Daniel Oliveira

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/**
 * @file model.hpp
 * @brief Carrega um model usando a biblioteca assimp com base em um diretório.
 *
 * @see model.cpp
 */

#pragma once
#include <assimp/scene.h>
#include <filesystem>
#include "commons_namespace.hpp"
#include "util/mesh.hpp"
#include <iostream>
#include "shader.hpp"
#include "model_pose.hpp"
#include <chrono>

namespace COMMONS_NS {
    class model {
    public:
        model() = default;
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
        void relocate_source_path(const std::filesystem::path& path) {
            source_path = path.string();
            directory = path.parent_path().string();
        }
        std::vector<mesh> meshes;

        void process_node(aiNode* node, const aiScene* scene);
        mesh process_mesh(aiMesh* mesh, const aiScene* scene);
        texture load_texture(aiMaterial*, const aiTextureType&);
        static bool hasTextura(aiMaterial*, const aiTextureType&);
    protected:
        void load_model_data(const std::string& path);
        std::shared_ptr<model_pose> m_pose;
        unsigned m_animation_clip = 0;
        std::chrono::steady_clock::time_point m_animation_start;
        std::string directory;
        std::string source_path;
        shader m_shader;
    };
}
