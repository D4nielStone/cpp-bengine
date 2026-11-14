#include <os/resource_files.hpp>
#include "glad.h"
#include "loaders/shader.hpp"
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>
#include <stdexcept>
#include <chrono>

using namespace COMMONS_NS;

namespace {
    std::unordered_map<unsigned int, std::unordered_map<std::string, int>> uniform_location_cache;
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
        if (ID) { uniform_location_cache.erase(ID); glDeleteProgram(ID); }
        ID = program; shaders[key] = ID; program = 0;
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
    glUniform1i(uniform_location(name), value);
}

void shader::setFloat(const std::string& name, const float& value) const {
    glUniform1f(uniform_location(name), value);
}

void shader::setInt(const std::string& name, const int& value) const {
    glUniform1i(uniform_location(name), value);
}

void shader::setMat4(const std::string& name, const float* value) const {
    glUniformMatrix4fv(uniform_location(name), 1, GL_FALSE, value);
}

void shader::setMat3(const std::string& name, const float* value) const {
    glUniformMatrix3fv(uniform_location(name), 1, GL_FALSE, value);
}

void shader::set_color(const std::string& name, const COMMONS_NS::color& color) const {
    glUniform4f(uniform_location(name), color.r, color.g, color.b, color.a);
}

void shader::setVec4(const std::string& name, const bgui::vec4& vec4 ) const {
    glUniform4f(uniform_location(name), vec4.x, vec4.y, vec4.z, vec4.w);
}
void shader::setVec3(const std::string& name, const float& r, const float& g, const float& b) const {
    glUniform3f(uniform_location(name), r, g, b);
}
void shader::setVec3(const std::string& name, const bgui::vec3& vector_type) const {
    glUniform3f(uniform_location(name), vector_type.x, vector_type.y, vector_type.z);
}

void shader::setVec2(const std::string& name, const float& r, const float& g) const {
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
