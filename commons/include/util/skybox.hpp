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
 * @file skybox.hpp
 */

#pragma once
#include "commons_namespace.hpp"
#include "loaders/shader.hpp"
#include "loaders/model.hpp"
#include <glm/glm.hpp>
#include <array>

namespace COMMONS_NS {
    class skybox : protected model
    {
        public:
        unsigned int id_skybox = 0;
        skybox() = default;
        ~skybox();
        // Faces: +X, -X, +Y, -Y, +Z, -Z. Empty/invalid sets use atmosphere.
        void draw(glm::mat4 view, glm::mat4 proj);
        void draw_background(glm::mat4 view, glm::mat4 proj,
            const std::array<std::string, 6>& faces, const std::string& root,
            bool enabled, const bgui::vec3& horizon, const bgui::vec3& zenith);
    private:
        unsigned int m_vao{0};
        bool m_owned{false}, m_attempted{false}, m_valid{false};
        std::array<std::string, 6> m_faces{};
        std::string m_root;
    };
}
