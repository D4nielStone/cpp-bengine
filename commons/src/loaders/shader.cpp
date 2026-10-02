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
 * @file shader.cpp
 */

#include "glad.h"
#include "loaders/shader.hpp"
#include <filesystem>
#include <stdexcept>

using namespace COMMONS_NS;

namespace {
    std::filesystem::path safe_absolute(const std::filesystem::path& p)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(p, ec);
        if (!ec)
            return absolute;
        return p.empty() ? std::filesystem::path{} : p.lexically_normal();
    }

    std::filesystem::path resolve_shader_path(const char* requested_path)
    {
        const std::filesystem::path requested(requested_path);
        const auto filename = requested.filename().string();

        std::string normalized_name = filename;
        if (filename == "skybox.vs")
            normalized_name = "skybox.vert";
        else if (filename == "skybox.fs")
            normalized_name = "skybox.frag";

        const std::filesystem::path shader_dir = "commons/assets/shaders";
        const std::vector<std::filesystem::path> candidates = {
            requested,
            std::filesystem::path("assets/shaders") / normalized_name,
            shader_dir / normalized_name,
            std::filesystem::path("../") / shader_dir / normalized_name,
            std::filesystem::path(COMMONS_SHADER_ASSET_DIR) / normalized_name,
            std::filesystem::path(COMMONS_SHADER_ASSET_DIR) / requested.filename()
        };

        for (const auto& candidate : candidates) {
            std::error_code ec;
            const auto absolute_candidate = safe_absolute(candidate);
            if (std::filesystem::is_regular_file(absolute_candidate, ec) || std::filesystem::is_regular_file(candidate, ec))
                return absolute_candidate;
        }

        throw std::runtime_error(
            "Shader file not found: " + std::string(requested_path) +
            ". Expected a runtime asset under assets/shaders or commons/assets/shaders."
        );
    }

    std::string read_shader_file(const std::filesystem::path& path)
    {
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("Failed to open shader file: " + path.string());

        std::stringstream source;
        source << file.rdbuf();
        return source.str();
    }
}

void COMMONS_NS::desload_shaders()
{
    shaders.clear();
}

shader_exception::shader_exception(const char* msg) : msg_(msg) {}

const char* shader_exception::what() const noexcept {
    return msg_.c_str();
}

shader::shader(const char* vertexPath, const char* fragmentPath) {
    compilar(vertexPath, fragmentPath);
}

void shader::compilar(const char* vertexPath, const char* fragmentPath) {
    vert = vertexPath; frag = fragmentPath;
    // Verifica se o shader já foi compilado
    if(shaders.find(fragmentPath) != shaders.end())
    {
        ID = shaders[fragmentPath];
        return;
    }
    // Cria o programa shader
    try {
        ID = glCreateProgram();
    }
    catch (const std::exception& e) {
        std::cerr << "Erro ao create shader_program: " << e.what() << std::endl;
    }

    const auto vertex_file = resolve_shader_path(vertexPath);
    const auto fragment_file = resolve_shader_path(fragmentPath);
    const std::string vertexCode = read_shader_file(vertex_file);
    const std::string fragmentCode = read_shader_file(fragment_file);
    const char* vertexshaderSource = vertexCode.c_str();
    const char* fragmentshaderSource = fragmentCode.c_str();
    // Compilação do Malha shader
    GLuint vertexshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexshader, 1, &vertexshaderSource, NULL);
    glCompileShader(vertexshader);
    if (!checkCompileErrors(vertexshader, "VERTEX")) return;

    // Compilação do Fragment shader
    GLuint fragmentshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentshader, 1, &fragmentshaderSource, NULL);
    glCompileShader(fragmentshader);
    if (!checkCompileErrors(fragmentshader, "FRAGMENT")) return;

    // Vinculação e linkagem dos shaders
    glAttachShader(ID, vertexshader);
    glAttachShader(ID, fragmentshader);
    glLinkProgram(ID);
    if (!checkLinkErrors(ID)) return;

    // Limpeza
    glDeleteShader(vertexshader);
    glDeleteShader(fragmentshader);


    shaders[fragmentPath] = ID;
}

void shader::use()
{
    if(shaders.find(frag) != shaders.end())
    {
        glUseProgram(ID);
    }
    else
    {
        compilar(vert.c_str(), frag.c_str());

    }
}

void shader::setBool(const std::string& name, const bool& value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void shader::setFloat(const std::string& name, const float& value) const {
    glUniform1f(glGetUniformLocation(ID, name.c_str()), value);
}

void shader::setInt(const std::string& name, const int& value) const {
    glUniform1i(glGetUniformLocation(ID, name.c_str()), value);
}

void shader::setMat4(const std::string& name, const float* value) const {
    glUniformMatrix4fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, value);
}

void shader::setMat3(const std::string& name, const float* value) const {
    glUniformMatrix3fv(glGetUniformLocation(ID, name.c_str()), 1, GL_FALSE, value);
}

void shader::set_color(const std::string& name, const COMMONS_NS::color& color) const {
    glUniform4f(glGetUniformLocation(ID, name.c_str()), color.r, color.g, color.b, color.a);
}

void shader::setVec4(const std::string& name, const fvector_type4& vec4 ) const {
    glUniform4f(glGetUniformLocation(ID, name.c_str()), vec4.x, vec4.y, vec4.z, vec4.w);
}
void shader::setVec3(const std::string& name, const float& r, const float& g, const float& b) const {
    glUniform3f(glGetUniformLocation(ID, name.c_str()), r, g, b);
}
void shader::setVec3(const std::string& name, const fvec3& vector_type) const {
    glUniform3f(glGetUniformLocation(ID, name.c_str()), vector_type.x, vector_type.y, vector_type.z);
}

void shader::setVec2(const std::string& name, const float& r, const float& g) const {
    glUniform2f(glGetUniformLocation(ID, name.c_str()), r, g);
}

bool shader::checkCompileErrors(unsigned int shader, const std::string& type) {
    GLint success;
    GLchar infoLog[1024];

    if (type != "PROGRAM") {
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::SHADER_COMPILATION_ERROR of type: " << type << ": " << infoLog << std::endl;
            return false;
        }
    }
    else {
        glGetProgramiv(shader, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shader, 1024, NULL, infoLog);
            std::cerr << "ERROR::PROGRAM_LINKING_ERROR: " << infoLog << std::endl;
            return false;
        }
    }
    return true;
}

bool shader::checkLinkErrors(unsigned int program) {
    GLint success;
    GLchar infoLog[1024];

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 1024, NULL, infoLog);
        std::cerr << "PROGRAM_LINKING_ERROR: " << infoLog << std::endl;
        return false;
    }
    return true;
}
