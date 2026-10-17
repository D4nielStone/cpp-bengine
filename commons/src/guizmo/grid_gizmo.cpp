#include "guizmo/grid_gizmo.hpp"

#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/gtc/type_ptr.hpp>

#include "components/camera.hpp"
#include "glad.h"

namespace {
    struct grid_vertex {
        float x;
        float y;
        float z;
        float r;
        float g;
        float b;
        float a;
    };

    GLuint compile_shader(const GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);

        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE)
            return shader;

        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::string log(static_cast<size_t>(log_length), '\0');
        if (log_length > 0)
            glGetShaderInfoLog(shader, log_length, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Failed to compile grid shader: " + log);
    }

    GLuint create_grid_program() {
        constexpr char vertex_source[] = R"(
            #version 330 core
            layout (location = 0) in vec3 position;
            layout (location = 1) in vec4 color;
            uniform mat4 view;
            uniform mat4 projection;
            out vec4 vertex_color;
            void main() {
                vertex_color = color;
                gl_Position = projection * view * vec4(position, 1.0);
            }
        )";
        constexpr char fragment_source[] = R"(
            #version 330 core
            in vec4 vertex_color;
            out vec4 fragment_color;
            void main() {
                fragment_color = vertex_color;
            }
        )";

        const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
        GLuint fragment_shader = 0;
        GLuint program = 0;
        try {
            fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
            program = glCreateProgram();
            glAttachShader(program, vertex_shader);
            glAttachShader(program, fragment_shader);
            glLinkProgram(program);

            GLint linked = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE) {
                GLint log_length = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
                std::string log(static_cast<size_t>(log_length), '\0');
                if (log_length > 0)
                    glGetProgramInfoLog(program, log_length, nullptr, log.data());
                throw std::runtime_error("Failed to link grid shader: " + log);
            }
        } catch (...) {
            if (program != 0)
                glDeleteProgram(program);
            if (fragment_shader != 0)
                glDeleteShader(fragment_shader);
            glDeleteShader(vertex_shader);
            throw;
        }

        glDetachShader(program, vertex_shader);
        glDetachShader(program, fragment_shader);
        glDeleteShader(vertex_shader);
        glDeleteShader(fragment_shader);
        return program;
    }

    struct saved_gl_state {
        GLint framebuffer;
        GLint viewport[4];
        GLint program;
        GLint vertex_array;
        GLint array_buffer;
        GLint depth_function;
        GLint blend_source;
        GLint blend_destination;
        GLboolean depth_test;
        GLboolean depth_write;
        GLboolean blend;
        GLboolean scissor_test;
    };

    saved_gl_state save_gl_state() {
        saved_gl_state state{};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state.framebuffer);
        glGetIntegerv(GL_VIEWPORT, state.viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vertex_array);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.array_buffer);
        glGetIntegerv(GL_DEPTH_FUNC, &state.depth_function);
        glGetIntegerv(GL_BLEND_SRC, &state.blend_source);
        glGetIntegerv(GL_BLEND_DST, &state.blend_destination);
        state.depth_test = glIsEnabled(GL_DEPTH_TEST);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depth_write);
        state.blend = glIsEnabled(GL_BLEND);
        state.scissor_test = glIsEnabled(GL_SCISSOR_TEST);
        return state;
    }

    void restore_gl_state(const saved_gl_state& state) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(state.framebuffer));
        glViewport(state.viewport[0], state.viewport[1], state.viewport[2], state.viewport[3]);
        glUseProgram(static_cast<GLuint>(state.program));
        glBindVertexArray(static_cast<GLuint>(state.vertex_array));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(state.array_buffer));
        glDepthFunc(static_cast<GLenum>(state.depth_function));
        glDepthMask(state.depth_write);
        if (state.depth_test)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);
        glBlendFunc(static_cast<GLenum>(state.blend_source), static_cast<GLenum>(state.blend_destination));
        if (state.blend)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);
        if (state.scissor_test)
            glEnable(GL_SCISSOR_TEST);
        else
            glDisable(GL_SCISSOR_TEST);
    }
}

editor::grid_gizmo::~grid_gizmo() {
    if (m_vbo != 0)
        glDeleteBuffers(1, &m_vbo);
    if (m_vao != 0)
        glDeleteVertexArrays(1, &m_vao);
    if (m_program != 0)
        glDeleteProgram(m_program);
}

void editor::grid_gizmo::draw(
    const COMMONS_NS::camera& camera,
    const bgui::vec4i& viewport)
{
    if (!enabled || spacing <= 0.f || extent <= 0.f || viewport.z <= 0 || viewport.w <= 0 ||
        !camera.flag_fb || camera.viewportFBO.x <= 0 || camera.viewportFBO.y <= 0)
        return;

    const int line_count = static_cast<int>(std::floor(extent / spacing));
    std::vector<grid_vertex> vertices;
    vertices.reserve(static_cast<size_t>(line_count * 2 + 1) * 4);

    const auto append_line = [&vertices](
        const float start_x, const float start_z,
        const float end_x, const float end_z,
        const float r, const float g, const float b, const float a)
    {
        vertices.push_back({start_x, 0.f, start_z, r, g, b, a});
        vertices.push_back({end_x, 0.f, end_z, r, g, b, a});
    };

    for (int index = -line_count; index <= line_count; ++index) {
        const float offset = static_cast<float>(index) * spacing;
        const bool axis = index == 0;
        const float gray = 0.55f;
        append_line(offset, -extent, offset, extent,
            axis ? 0.78f : gray, axis ? 0.34f : 0.58f, axis ? 0.31f : 0.62f,
            axis ? 0.8f : 0.55f);
        append_line(-extent, offset, extent, offset,
            axis ? 0.32f : gray, axis ? 0.55f : 0.58f, axis ? 0.82f : 0.62f,
            axis ? 0.8f : 0.55f);
    }

    if (m_program == 0) {
        m_program = create_grid_program();
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
    }

    const auto state = save_gl_state();
    glBindFramebuffer(GL_FRAMEBUFFER, camera.fbo);
    glViewport(0, 0, camera.viewportFBO.x, camera.viewportFBO.y);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);

    glUseProgram(m_program);
    glUniformMatrix4fv(glGetUniformLocation(m_program, "view"), 1, GL_FALSE,
        glm::value_ptr(camera.viewMatrix));
    glUniformMatrix4fv(glGetUniformLocation(m_program, "projection"), 1, GL_FALSE,
        glm::value_ptr(camera.projMatriz));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(grid_vertex)),
        vertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(grid_vertex),
        reinterpret_cast<void*>(offsetof(grid_vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(grid_vertex),
        reinterpret_cast<void*>(offsetof(grid_vertex, r)));
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));

    restore_gl_state(state);
}
