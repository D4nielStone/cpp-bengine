/** @copyright
 * MIT License
 * Copyright (c) 2025 Daniel Oliveira
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
/**
 * @file mesh.cpp
 */

#include "util/material.hpp"
#include <utils/texture.hpp>
#include "util/pbr_material.hpp"
#include <unordered_set>
#include <algorithm>
#include <filesystem>
using namespace COMMONS_NS;

material::material(std::map<std::string, uniform> unifs) {
    for(auto& [nome, u] : unifs) {
        uniforms[nome] = u;
    }
}
void material::set_uniform(const std::string& nome, const uniform& prop) {
    uniforms[nome] = prop;
}

void material::set_texture(const std::string& nome, const texture& prop) {
    auto normalized = prop;
    // Imported materials and saved scenes may use Windows separators on Linux.
    std::replace(normalized.path.begin(), normalized.path.end(), '\\', '/');
    normalized.path = std::filesystem::path(normalized.path).lexically_normal().string();
    textures[nome] = std::move(normalized);
}

uniform material::getUniforme(const std::string& nome) const {
    auto it = uniforms.find(nome);
    if(it != uniforms.end())
        return it->second;
    else
        return uniform(1.f);
}

texture material::getTextura(const std::string& nome) const {
    auto it = textures.find(nome);
    if(it != textures.end())
        return it->second;
    else
        return texture();
}
void material::usar(shader& shader) {
    apply(shader);
}

void material::apply(shader& shader) const {
    shader.use();

    // Reset optional state for each draw: materials share the shader program.
    shader.setBool("uvMundo", false);
    for (const auto& factor : texture_factors)
        shader.setFloat(std::string("material.") + factor.name, pbr_value(*this, factor));

    for (const auto& [nome, prop] : uniforms) {
        switch (prop.m_type) {
            case uniform::type::Float:
                shader.setFloat(nome, prop.valor.f); break;
            case uniform::type::Float_ptr:
                shader.setFloat(nome, *prop.ponteiro.f_ptr); break;

            case uniform::type::Int:
                shader.setInt(nome, prop.valor.i); break;
            case uniform::type::Int_ptr:
                shader.setInt(nome, *prop.ponteiro.i_ptr); break;

            case uniform::type::Bool:
                shader.setBool(nome, prop.valor.b); break;
            case uniform::type::Bool_ptr:
                shader.setBool(nome, *prop.ponteiro.b_ptr); break;

            case uniform::type::Vet2:
                shader.setVec2(nome, prop.valor.vec2.x, prop.valor.vec2.y); break;
            case uniform::type::Vet2_ptr:
                shader.setVec2(nome, prop.ponteiro.vec2_ptr->x, prop.ponteiro.vec2_ptr->y); break;

            case uniform::type::Vet3:
                shader.setVec3(nome, prop.valor.vec3); break;
            case uniform::type::Vet3_ptr:
                shader.setVec3(nome, *prop.ponteiro.vec3_ptr); break;

            case uniform::type::Vet4:
                shader.setVec4(nome, prop.valor.vec4); break;
            case uniform::type::Vet4_ptr:
                shader.setVec4(nome, *prop.ponteiro.vec4_ptr); break;

            case uniform::type::Cor:
                shader.set_color(nome, prop.valor.color_value); break;
            case uniform::type::Cor_ptr:
                shader.set_color(nome, *prop.ponteiro.color_ptr); break;

            case uniform::type::Mat4:
                shader.setMat4(nome, glm::value_ptr(prop.valor.mat4)); break;
            case uniform::type::Mat4_ptr:
                shader.setMat4(nome, glm::value_ptr(*prop.ponteiro.mat4_ptr)); break;
        }
    }

    for (const auto& map : pbr_maps)
        if (!textures.contains(map.name))
            shader.setBool(std::string("use_") + map.name, false);

    int slot = 0;
    for (const auto& [nome, tex] : textures) {
        try {
            tex.bind(slot);
            shader.setInt(nome, slot++);
        } catch (const std::exception& error) {
            glActiveTexture(GL_TEXTURE0 + slot++);
            glBindTexture(GL_TEXTURE_2D, 0);
            shader.setBool("use_" + nome, false);
            static std::unordered_set<std::string> reported;
            if (reported.insert(tex.path).second)
                std::cerr << "[material] " << error.what() << '\n';
        }
    }
}

namespace {
bool same_uniform(const uniform& left, const uniform& right) {
    if (left.m_type != right.m_type) return false;
    using enum uniform::type;
    switch (left.m_type) {
        case Float: return left.valor.f == right.valor.f;
        case Int: return left.valor.i == right.valor.i;
        case Bool: return left.valor.b == right.valor.b;
        case Vet2: return left.valor.vec2 == right.valor.vec2;
        case Vet3: return left.valor.vec3 == right.valor.vec3;
        case Vet4: return left.valor.vec4 == right.valor.vec4;
        case Cor: {
            const auto& a = left.valor.color_value;
            const auto& b = right.valor.color_value;
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }
        case Mat4: return left.valor.mat4 == right.valor.mat4;
        case Float_ptr: return left.ponteiro.f_ptr == right.ponteiro.f_ptr;
        case Int_ptr: return left.ponteiro.i_ptr == right.ponteiro.i_ptr;
        case Bool_ptr: return left.ponteiro.b_ptr == right.ponteiro.b_ptr;
        case Vet2_ptr: return left.ponteiro.vec2_ptr == right.ponteiro.vec2_ptr;
        case Vet3_ptr: return left.ponteiro.vec3_ptr == right.ponteiro.vec3_ptr;
        case Vet4_ptr: return left.ponteiro.vec4_ptr == right.ponteiro.vec4_ptr;
        case Cor_ptr: return left.ponteiro.color_ptr == right.ponteiro.color_ptr;
        case Mat4_ptr: return left.ponteiro.mat4_ptr == right.ponteiro.mat4_ptr;
    }
    return false;
}

bool same_texture(const texture& left, const texture& right) {
    if (!(left == right) || left.filtering != right.filtering) return false;
    if (left.embedded == right.embedded) return true;
    if (!left.embedded || !right.embedded) return false;
    const auto& a = *left.embedded;
    const auto& b = *right.embedded;
    return a.m_size == b.m_size && a.m_path == b.m_path && a.m_id == b.m_id &&
        a.m_external == b.m_external && a.m_has_alpha == b.m_has_alpha &&
        a.m_use_red_channel == b.m_use_red_channel &&
        a.m_generate_mipmap == b.m_generate_mipmap && a.m_buffer == b.m_buffer;
}
}

bool material::same_configuration(const material& other) const {
    if (this == &other) return true;
    if (uniforms.size() != other.uniforms.size() || textures.size() != other.textures.size())
        return false;
    for (const auto& [name, value] : uniforms) {
        const auto match = other.uniforms.find(name);
        if (match == other.uniforms.end() || !same_uniform(value, match->second))
            return false;
    }
    for (const auto& [name, value] : textures) {
        const auto match = other.textures.find(name);
        if (match == other.textures.end() || !same_texture(value, match->second))
            return false;
    }
    return true;
}

material_binding::material_binding(const material& material, shader& shader)
    : m_textures(material.textures.size()) {
    glGetIntegerv(GL_ACTIVE_TEXTURE, &m_active_texture);
    for (std::size_t slot = 0; slot < m_textures.size(); ++slot) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_textures[slot].binding);
    }
    glActiveTexture(m_active_texture);
    material.apply(shader);

    std::size_t slot = 0;
    for (const auto& [name, tex] : material.textures) {
        auto& state = m_textures[slot];
        glActiveTexture(GL_TEXTURE0 + slot++);
        GLint id = 0;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &id);
        if (!id) continue;
        state.parameters_changed = true;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &state.s);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &state.t);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &state.min_filter);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &state.mag_filter);
        if (tex.filtering != texture_filter::original) {
            if (tex.filtering == texture_filter::trilinear) {
                GLint width = 0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &width);
                if (!width) {
                    GLint base_width = 0, base_height = 0;
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &base_width);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &base_height);
                    if (base_width > 1 || base_height > 1) glGenerateMipmap(GL_TEXTURE_2D);
                }
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                tex.filtering == texture_filter::trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glActiveTexture(m_active_texture);
}

material_binding::~material_binding() {
    for (std::size_t slot = m_textures.size(); slot-- > 0;) {
        const auto& state = m_textures[slot];
        glActiveTexture(GL_TEXTURE0 + slot);
        if (state.parameters_changed) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, state.min_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, state.mag_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.t);
        }
        glBindTexture(GL_TEXTURE_2D, state.binding);
    }
    glActiveTexture(m_active_texture);
}
