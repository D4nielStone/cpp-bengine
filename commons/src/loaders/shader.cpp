#include <os/resource_files.hpp>
#include "glad.h"
#include "loaders/shader.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>
#include <stdexcept>
#include <chrono>

using namespace COMMONS_NS;

namespace {
    std::unordered_map<unsigned int, std::unordered_map<std::string, int>> uniform_location_cache;
    std::unordered_map<GLuint, std::unordered_map<std::string, GLenum>> uniform_types;
    struct uniform_default {
        GLint location;
        GLenum type;
        std::array<float, 16> numbers{};
        GLint integer{};
    };
    std::unordered_map<GLuint, std::vector<uniform_default>> uniform_defaults;

    std::vector<uniform_default> capture_uniform_defaults(GLuint program) {
        std::vector<uniform_default> result;
        GLint count = 0, length = 0;
        glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
        glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &length);
        std::vector<char> buffer(std::max(length, 1));
        for (GLint i = 0; i < count; ++i) {
            GLsizei written; GLint size; GLenum type;
            glGetActiveUniform(program, i, buffer.size(), &written, &size, &type, buffer.data());
            const bool integer = type == GL_INT || type == GL_BOOL || type == GL_SAMPLER_2D || type == GL_SAMPLER_CUBE;
            const std::string name(buffer.data(), written);
            for (GLint element = 0; element < size; ++element) {
                auto indexed = name;
                const auto offset = indexed.find("[0]");
                if (offset != std::string::npos) indexed.replace(offset, 3, "[" + std::to_string(element) + "]");
                const auto location = glGetUniformLocation(program, indexed.c_str());
                if (location < 0) continue;
                uniform_types[program][indexed] = type;
                if (indexed.ends_with("[0]")) uniform_types[program][indexed.substr(0, indexed.size() - 3)] = type;
                // The renderer uploads every matrix addressed by an instance draw.
                if (name == "transformacoes[0]" && type == GL_FLOAT_MAT4) continue;
                if (!integer && type != GL_FLOAT && type != GL_FLOAT_VEC2 && type != GL_FLOAT_VEC3 && type != GL_FLOAT_VEC4 && type != GL_FLOAT_MAT4) continue;
                uniform_default value{location, type};
                if (integer) glGetUniformiv(program, location, &value.integer);
                else glGetUniformfv(program, location, value.numbers.data());
                result.push_back(value);
            }
        }
        return result;
    }

    bool uniform_type_matches(GLuint program, const std::string& name, GLenum expected) {
        const auto found = uniform_types.find(program);
        if (found == uniform_types.end()) return program != 0;
        const auto entry = found->second.find(name);
        if (entry == found->second.end()) return false;
        const auto actual = entry->second;
        if (actual == expected) return true;
        if ((expected == GL_FLOAT && actual == GL_BOOL) ||
            (expected == GL_FLOAT_VEC2 && actual == GL_BOOL_VEC2) ||
            (expected == GL_FLOAT_VEC3 && actual == GL_BOOL_VEC3) ||
            (expected == GL_FLOAT_VEC4 && actual == GL_BOOL_VEC4)) return true;
        if (expected != GL_INT) return false;
        switch (actual) {
        case GL_BOOL:
        case GL_SAMPLER_1D: case GL_SAMPLER_2D: case GL_SAMPLER_3D:
        case GL_SAMPLER_CUBE: case GL_SAMPLER_1D_SHADOW: case GL_SAMPLER_2D_SHADOW:
        case GL_SAMPLER_1D_ARRAY: case GL_SAMPLER_2D_ARRAY: case GL_SAMPLER_1D_ARRAY_SHADOW:
        case GL_SAMPLER_2D_ARRAY_SHADOW: case GL_SAMPLER_CUBE_SHADOW: case GL_INT_SAMPLER_1D:
        case GL_INT_SAMPLER_2D: case GL_INT_SAMPLER_3D: case GL_INT_SAMPLER_CUBE:
        case GL_INT_SAMPLER_1D_ARRAY: case GL_INT_SAMPLER_2D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_1D:
        case GL_UNSIGNED_INT_SAMPLER_2D: case GL_UNSIGNED_INT_SAMPLER_3D: case GL_UNSIGNED_INT_SAMPLER_CUBE:
        case GL_UNSIGNED_INT_SAMPLER_1D_ARRAY: case GL_UNSIGNED_INT_SAMPLER_2D_ARRAY: case GL_SAMPLER_2D_RECT:
        case GL_SAMPLER_2D_RECT_SHADOW: case GL_SAMPLER_BUFFER: case GL_INT_SAMPLER_2D_RECT:
        case GL_INT_SAMPLER_BUFFER: case GL_UNSIGNED_INT_SAMPLER_2D_RECT: case GL_UNSIGNED_INT_SAMPLER_BUFFER:
            return true;
        default: return false;
        }
    }

    struct shader_revision {
        std::filesystem::file_time_type vertex{}, fragment{};
        std::chrono::steady_clock::time_point checked{};
        bool attempted{false};
    };
    std::unordered_map<std::string, shader_revision> shader_revisions;
    std::string program_key(const std::string& vertex, const std::string& fragment) {
        return vertex + "\n" + fragment;
    }

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
            if (bgui::resource_files::exists(absolute_candidate) || bgui::resource_files::exists(candidate))
                return absolute_candidate;
        }

        throw std::runtime_error(
            "Shader file not found: " + std::string(requested_path) +
            ". Expected a runtime asset under assets/shaders or commons/assets/shaders."
        );
    }

    std::string read_shader_file(const std::filesystem::path& path)
    {
        auto file = bgui::resource_files::stream(path);
        if (!file)
            throw std::runtime_error("Failed to open shader file: " + path.string());

        std::stringstream source;
        source << file.rdbuf();
        return source.str();
    }
}

void COMMONS_NS::desload_shaders()
{
    if (glad_glGetString && glGetString(GL_VERSION))
        for (const auto& [key, program] : shaders) if (program) glDeleteProgram(program);
    shaders.clear();
    shader_revisions.clear();
    uniform_location_cache.clear();
    uniform_defaults.clear();
    uniform_types.clear();
}

shader_exception::shader_exception(const char* msg) : msg_(msg) {}

const char* shader_exception::what() const noexcept {
    return msg_.c_str();
}

shader::shader(const char* vertexPath, const char* fragmentPath) : vert(vertexPath), frag(fragmentPath), ID(0) {
    if (glad_glCreateProgram != nullptr)
        compilar(vertexPath, fragmentPath);
}

void shader::compilar(const char* vertexPath, const char* fragmentPath) {
    vert = vertexPath; frag = fragmentPath;
    if (!glad_glCreateProgram || !glGetString(GL_VERSION)) { ID = 0; return; }
    const auto key = program_key(vert, frag);
    auto& revision = shader_revisions[key];
    const auto cached = shaders.find(key);
    ID = cached == shaders.end() ? 0 : cached->second;
    const auto now = std::chrono::steady_clock::now();
    if (now - revision.checked < std::chrono::milliseconds(500)) return;
    revision.checked = now;
    GLuint vertex = 0, fragment = 0, program = 0;
    try {
        const auto vertex_file = resolve_shader_path(vertexPath);
        const auto fragment_file = resolve_shader_path(fragmentPath);
        std::error_code error;
        const auto vertex_time = std::filesystem::last_write_time(vertex_file, error);
        const auto fragment_time = std::filesystem::last_write_time(fragment_file, error);
        if (revision.attempted && revision.vertex == vertex_time && revision.fragment == fragment_time) return;
        revision.attempted = true; revision.vertex = vertex_time; revision.fragment = fragment_time;
        const auto vertex_code = read_shader_file(vertex_file), fragment_code = read_shader_file(fragment_file);
        const auto* vs = vertex_code.c_str(); const auto* fs = fragment_code.c_str();
        vertex = glCreateShader(GL_VERTEX_SHADER); fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(vertex, 1, &vs, nullptr); glCompileShader(vertex);
        glShaderSource(fragment, 1, &fs, nullptr); glCompileShader(fragment);
        if (!checkCompileErrors(vertex, "VERTEX") || !checkCompileErrors(fragment, "FRAGMENT"))
            throw std::runtime_error("GLSL compilation failed: " + frag);
        program = glCreateProgram();
        glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
        if (!checkLinkErrors(program)) throw std::runtime_error("GLSL link failed: " + frag);
        auto defaults = capture_uniform_defaults(program);
        if (ID) { uniform_location_cache.erase(ID); uniform_defaults.erase(ID); uniform_types.erase(ID); glDeleteProgram(ID); }
        ID = program; shaders[key] = ID; uniform_defaults[ID] = std::move(defaults); program = 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    if (vertex) glDeleteShader(vertex);
    if (fragment) glDeleteShader(fragment);
    if (program) glDeleteProgram(program);
}

void shader::use() {
    if (!glad_glUseProgram) return;
    const auto found = shaders.find(program_key(vert, frag));
    if (found != shaders.end()) ID = found->second;
    else compilar(vert.c_str(), frag.c_str());
    if (ID) glUseProgram(ID);
}

void shader::reset_uniforms() {
    use();
    const auto found = uniform_defaults.find(ID);
    if (found == uniform_defaults.end()) return;
    for (const auto& value : found->second) {
        const auto location = value.location;
        const auto* n = value.numbers.data();
        switch (value.type) {
        case GL_FLOAT: glUniform1fv(location, 1, n); break;
        case GL_FLOAT_VEC2: glUniform2fv(location, 1, n); break;
        case GL_FLOAT_VEC3: glUniform3fv(location, 1, n); break;
        case GL_FLOAT_VEC4: glUniform4fv(location, 1, n); break;
        case GL_FLOAT_MAT4: glUniformMatrix4fv(location, 1, GL_FALSE, n); break;
        default: glUniform1i(location, value.integer); break;
        }
    }
}

std::vector<shader::uniform_info> shader::active_uniforms() {
    std::vector<uniform_info> result;
    if (!glad_glGetProgramiv || !glGetString(GL_VERSION)) return result;
    compilar(vert.c_str(), frag.c_str());
    if (!ID) return result;
    GLint count = 0, length = 0;
    glGetProgramiv(ID, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(ID, GL_ACTIVE_UNIFORM_MAX_LENGTH, &length);
    std::vector<char> name(std::max(length, 1));
    for (GLint i = 0; i < count; ++i) {
        GLsizei written = 0; GLint size = 0; GLenum type = 0;
        glGetActiveUniform(ID, i, name.size(), &written, &size, &type, name.data());
        if (glGetUniformLocation(ID, name.data()) < 0) continue;
        std::string kind;
        switch (type) {
        case GL_FLOAT: kind = "float"; break;
        case GL_INT: kind = "int"; break;
        case GL_BOOL: kind = "bool"; break;
        case GL_FLOAT_VEC2: kind = "vec2"; break;
        case GL_FLOAT_VEC3: kind = "vec3"; break;
        case GL_FLOAT_VEC4: kind = "vec4"; break;
        case GL_FLOAT_MAT4: kind = "mat4"; break;
        case GL_SAMPLER_2D: kind = "sampler2D"; break;
        case GL_SAMPLER_CUBE: kind = "samplerCube"; break;
        default: kind = "unsupported"; break;
        }
        result.push_back({std::string(name.data(), written), std::move(kind), size});
    }
    return result;
}

int shader::uniform_location(const std::string& name) const {
    auto& locations = uniform_location_cache[ID];
    const auto it = locations.find(name);
    if (it != locations.end())
        return it->second;

    const int location = glGetUniformLocation(ID, name.c_str());
    locations.emplace(name, location);
    return location;
}

void shader::setBool(const std::string& name, const bool& value) const {
    if (!uniform_type_matches(ID, name, GL_INT)) return;
    glUniform1i(uniform_location(name), value);
}

void shader::setFloat(const std::string& name, const float& value) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT)) return;
    glUniform1f(uniform_location(name), value);
}

void shader::setInt(const std::string& name, const int& value) const {
    if (!uniform_type_matches(ID, name, GL_INT)) return;
    glUniform1i(uniform_location(name), value);
}

void shader::setMat4(const std::string& name, const float* value) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_MAT4)) return;
    glUniformMatrix4fv(uniform_location(name), 1, GL_FALSE, value);
}

void shader::setMat3(const std::string& name, const float* value) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_MAT3)) return;
    glUniformMatrix3fv(uniform_location(name), 1, GL_FALSE, value);
}

void shader::set_color(const std::string& name, const COMMONS_NS::color& color) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_VEC4)) return;
    glUniform4f(uniform_location(name), color.r, color.g, color.b, color.a);
}

void shader::setVec4(const std::string& name, const bgui::vec4& vec4 ) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_VEC4)) return;
    glUniform4f(uniform_location(name), vec4.x, vec4.y, vec4.z, vec4.w);
}
void shader::setVec3(const std::string& name, const float& r, const float& g, const float& b) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_VEC3)) return;
    glUniform3f(uniform_location(name), r, g, b);
}
void shader::setVec3(const std::string& name, const bgui::vec3& vector_type) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_VEC3)) return;
    glUniform3f(uniform_location(name), vector_type.x, vector_type.y, vector_type.z);
}

void shader::setVec2(const std::string& name, const float& r, const float& g) const {
    if (!uniform_type_matches(ID, name, GL_FLOAT_VEC2)) return;
    glUniform2f(uniform_location(name), r, g);
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
