#include "systems/render_system.hpp"

#include <chrono>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "components/camera.hpp"
#include "components/hud.hpp"
#include <os/font.hpp>
#include <codecvt>
#include <locale>
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "debugging/debug.hpp"
#include "glad.h"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace COMMONS_NS;

namespace {
bool batchable_material(const material& material) {
    // Units 14 and 15 belong to the shadow passes.
    return material.textures.size() <= 14 &&
        std::all_of(material.textures.begin(), material.textures.end(), [](const auto& entry) {
            return entry.first.starts_with("tex_");
        }) &&
        std::all_of(material.uniforms.begin(), material.uniforms.end(), [](const auto& entry) {
            const auto& name = entry.first;
            return name == "uvMundo" || name.starts_with("material.") || name.starts_with("use_tex_");
        });
}

float finite_clamp(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

struct frustum_plane {
    glm::vec3 normal{0.f};
    float distance{0.f};
};

std::array<frustum_plane, 6> extract_frustum_planes(const glm::mat4& view, const glm::mat4& projection) {
    const glm::mat4 inverse = glm::inverse(projection * view);
    const std::array<glm::vec4, 8> ndc_corners = {
        glm::vec4{-1.f, -1.f, -1.f, 1.f},
        glm::vec4{ 1.f, -1.f, -1.f, 1.f},
        glm::vec4{-1.f,  1.f, -1.f, 1.f},
        glm::vec4{ 1.f,  1.f, -1.f, 1.f},
        glm::vec4{-1.f, -1.f,  1.f, 1.f},
        glm::vec4{ 1.f, -1.f,  1.f, 1.f},
        glm::vec4{-1.f,  1.f,  1.f, 1.f},
        glm::vec4{ 1.f,  1.f,  1.f, 1.f}
    };

    std::array<glm::vec3, 8> world_corners{};
    for (std::size_t index = 0; index < world_corners.size(); ++index) {
        const glm::vec4 world_space = inverse * ndc_corners[index];
        world_corners[index] = glm::vec3(world_space) / std::max(world_space.w, 1e-6f);
    }

    glm::vec3 frustum_center{0.f};
    for (const auto& corner : world_corners)
        frustum_center += corner;
    frustum_center /= static_cast<float>(world_corners.size());

    const auto build_plane = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
        glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
        const glm::vec3 plane_point = (a + b + c) / 3.f;
        if (glm::dot(normal, frustum_center - plane_point) < 0.f)
            normal *= -1.f;
        const float distance = -glm::dot(normal, a);
        return frustum_plane{normal, distance};
    };

    return {
        build_plane(world_corners[0], world_corners[2], world_corners[6]),
        build_plane(world_corners[1], world_corners[5], world_corners[7]),
        build_plane(world_corners[0], world_corners[1], world_corners[5]),
        build_plane(world_corners[2], world_corners[6], world_corners[7]),
        build_plane(world_corners[0], world_corners[4], world_corners[6]),
        build_plane(world_corners[1], world_corners[3], world_corners[7])
    };
}

bool sphere_in_frustum(const glm::vec3& center, float radius, const std::array<frustum_plane, 6>& planes) {
    for (const auto& plane : planes) {
        const float signed_distance = glm::dot(plane.normal, center) + plane.distance;
        if (signed_distance < -radius)
            return false;
    }
    return true;
}

std::pair<glm::vec3, float> model_bounds_in_world_space(const renderer& renderer_component, const transform& transform_component) {
    glm::vec3 minimum{std::numeric_limits<float>::infinity()};
    glm::vec3 maximum{-std::numeric_limits<float>::infinity()};
    bool has_vertices = false;

    if (renderer_component.m_modelo) {
        for (const auto& mesh : renderer_component.m_modelo->meshes) {
            for (const auto& vertex : mesh.vertices()) {
                const glm::vec3 position{vertex.position.x, vertex.position.y, vertex.position.z};
                minimum = glm::min(minimum, position);
                maximum = glm::max(maximum, position);
                has_vertices = true;
            }
        }
    }

    if (!has_vertices) {
        const auto position = transform_component.get_world_position();
        const glm::vec3 world_center{position.x, position.y, position.z};
        return {world_center, 0.f};
    }

    const glm::vec3 local_center = (minimum + maximum) * 0.5f;
    const auto world = transform_component.get_world_model_matrix();
    const glm::vec4 world_center = world * glm::vec4(local_center, 1.f);

    float radius = 0.f;
    for (const auto& mesh : renderer_component.m_modelo->meshes) {
        for (const auto& vertex : mesh.vertices()) {
            const glm::vec3 position{vertex.position.x, vertex.position.y, vertex.position.z};
            const float distance = glm::length(glm::mat3(world) * (position - local_center));
            radius = std::max(radius, distance);
        }
    }

    return {glm::vec3(world_center), radius};
}
}

render_system::~render_system() {
    // Systems may outlive the window during application shutdown.
    if (glad_glGetString && glGetString(GL_VERSION)) {
        for (auto& [widget, mesh] : m_text_meshes) {
            glDeleteBuffers(1, &mesh.buffer);
            glDeleteVertexArrays(1, &mesh.vao);
        }
        if (m_occlusion_depth) glDeleteTextures(1, &m_occlusion_depth);
        if (m_occlusion_vao) glDeleteVertexArrays(1, &m_occlusion_vao);
        for (auto* map : {&m_directional_shadow, &m_spot_shadow}) {
            if (map->texture) glDeleteTextures(1, &map->texture);
            if (map->fbo) glDeleteFramebuffers(1, &map->fbo);
        }
    }
}

bool render_system::prepare_depth_map(depth_map& map, int width, int height) {
    if (map.fbo && map.width == width && map.height == height) return true;
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    if (width <= 0 || height <= 0 || width > max_texture_size || height > max_texture_size)
        return false;
    GLint framebuffer, read_framebuffer, texture;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!map.fbo) glGenFramebuffers(1, &map.fbo);
    if (!map.texture) glGenTextures(1, &map.texture);
    glBindTexture(GL_TEXTURE_2D, map.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, map.fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, map.texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (!complete) {
        glDeleteTextures(1, &map.texture);
        glDeleteFramebuffers(1, &map.fbo);
        map.texture = map.fbo = 0;
    }
    map.width = complete ? width : 0;
    map.height = complete ? height : 0;
    return complete;
}

void render_system::apply_camera_occlusion(const camera& view) {
    const auto& settings = view.config.post_processing;
    if (!settings.valid() || settings.occlusion == ambient_occlusion_mode::off || settings.strength == 0.f)
        return;

    GLint viewport[4], draw_fb, read_fb;
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
    GLint max_texture_size, depth_bits;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, draw_fb ? GL_DEPTH_ATTACHMENT : GL_DEPTH,
        GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth_bits);
    const int width = viewport[2], height = viewport[3];
    if (depth_bits == 0 || width <= 0 || height <= 0 || width > max_texture_size || height > max_texture_size)
        return;

    shader program("ambient_occlusion.vert", "ambient_occlusion.frag");
    if (!program.ID) return;

    GLint previous_program, vao, active_texture, texture;
    glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!m_occlusion_depth) glGenTextures(1, &m_occlusion_depth);
    glBindTexture(GL_TEXTURE_2D, m_occlusion_depth);
    // Retain enough storage for the largest view so cameras of different sizes can share it.
    if (width > m_occlusion_width || height > m_occlusion_height) {
        GLint unpack_buffer;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
            std::max(width, m_occlusion_width), std::max(height, m_occlusion_height), 0,
            GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &m_occlusion_width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &m_occlusion_height);
        if (width > m_occlusion_width || height > m_occlusion_height) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glActiveTexture(active_texture);
            return;
        }
    }
    // Sample the actual scene depth, including custom shaders and animated geometry.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, draw_fb);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width, height);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);

    const bool depth = glIsEnabled(GL_DEPTH_TEST);
    const bool cull = glIsEnabled(GL_CULL_FACE);
    const bool stencil = glIsEnabled(GL_STENCIL_TEST);
    const bool blend = glIsEnabled(GL_BLEND);
    GLint src_rgb, dst_rgb, src_alpha, dst_alpha, equation_rgb, equation_alpha;
    glGetIntegerv(GL_BLEND_SRC_RGB, &src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &src_alpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dst_alpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &equation_rgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation_alpha);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ZERO, GL_SRC_COLOR, GL_ZERO, GL_ONE);

    if (!m_occlusion_vao) glGenVertexArrays(1, &m_occlusion_vao);
    glBindVertexArray(m_occlusion_vao);
    program.use();
    program.setInt("sceneDepth", 0);
    program.setVec2("viewportSize", static_cast<float>(width), static_cast<float>(height));
    program.setInt("mode", static_cast<int>(settings.occlusion));
    program.setFloat("radius", settings.radius);
    program.setFloat("strength", settings.strength);
    program.setMat4("projection", glm::value_ptr(view.projMatriz));
    const auto inverse = glm::inverse(view.projMatriz);
    program.setMat4("inverseProjection", glm::value_ptr(inverse));
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBlendEquationSeparate(equation_rgb, equation_alpha);
    glBlendFuncSeparate(src_rgb, dst_rgb, src_alpha, dst_alpha);
    if (!blend) glDisable(GL_BLEND);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (stencil) glEnable(GL_STENCIL_TEST);
    glUseProgram(previous_program);
    glBindVertexArray(vao);
    glBindTexture(GL_TEXTURE_2D, texture);
    glActiveTexture(active_texture);
}

void render_system::setup(const std::shared_ptr<ecs>&) {
}

void render_system::update(const std::shared_ptr<ecs>& reg) { render(reg,nullptr,0,{}); }

void render_system::render_view(const std::shared_ptr<ecs>& reg, camera& view,
    std::uint32_t replacement_entity, std::shared_ptr<renderer> replacement) {
    render_view(reg, view, replacement_entity, std::move(replacement), {});
}

void render_system::render_view(const std::shared_ptr<ecs>& reg, camera& view,
    std::uint32_t replacement_entity, std::shared_ptr<renderer> replacement,
    const std::shared_ptr<transform>& replacement_transform) {
    struct state_guard {
        GLint draw_fb,read_fb,renderbuffer,viewport[4],program,vao,array_buffer,active,depth_func,scissor[4];
        std::array<GLint,16> textures{},cubemaps{};
        GLboolean depth,blend,cull,scissor_test,srgb,depth_mask,color_mask[4];
        GLfloat clear_color[4];
        state_guard() {
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&draw_fb);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&read_fb);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
            glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_DEPTH_FUNC,&depth_func);
            glGetIntegerv(GL_SCISSOR_BOX,scissor);glGetFloatv(GL_COLOR_CLEAR_VALUE,clear_color);
            glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);glGetBooleanv(GL_COLOR_WRITEMASK,color_mask);
            depth=glIsEnabled(GL_DEPTH_TEST);blend=glIsEnabled(GL_BLEND);cull=glIsEnabled(GL_CULL_FACE);
            scissor_test=glIsEnabled(GL_SCISSOR_TEST);srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB);
            for(unsigned i=0;i<16;++i) { glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP,&cubemaps[i]); }
            glActiveTexture(active);
        }
        ~state_guard() {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,draw_fb);glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fb);glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
            glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
            glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,array_buffer);glDepthFunc(depth_func);glDepthMask(depth_mask);
            glColorMask(color_mask[0],color_mask[1],color_mask[2],color_mask[3]);glClearColor(clear_color[0],clear_color[1],clear_color[2],clear_color[3]);
            glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
            const auto restore=[](GLenum cap,bool enabled){ if(enabled)glEnable(cap);else glDisable(cap); };
            restore(GL_DEPTH_TEST,depth);restore(GL_BLEND,blend);restore(GL_CULL_FACE,cull);restore(GL_SCISSOR_TEST,scissor_test);restore(GL_FRAMEBUFFER_SRGB,srgb);
            for(unsigned i=0;i<16;++i) { glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);glBindTexture(GL_TEXTURE_CUBE_MAP,cubemaps[i]); }
            glActiveTexture(active);
        }
    } guard;
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    render(reg, &view, replacement_entity, replacement, replacement_transform);
}

namespace {
std::vector<float> text_geometry(const bgui::font& font, const std::string& text) {
    std::u32string codepoints;
    try {
        codepoints = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{}.from_bytes(text);
    } catch (const std::range_error&) { return {}; }
    const int atlas_width = static_cast<int>(font.atlas.m_size.x);
    const int atlas_height = static_cast<int>(font.atlas.m_size.y);
    if (!font.atlas.m_use_red_channel || atlas_width <= 0 || atlas_height <= 0 ||
        font.atlas.m_buffer.size() != std::size_t(atlas_width) * atlas_height) return {};
    std::vector<float> vertices;
    const auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, float shade) {
        for (const auto& point : {a, b, c, a, c, d})
            vertices.insert(vertices.end(), {point.x, point.y, point.z, shade});
    };
    float pen_x = 0.f, pen_y = 0.f;
    const float line_height = font.ascent + font.descent + font.line_gap;
    for (const auto codepoint : codepoints) {
        if (vertices.size() >= 1'000'000) break;
        if (codepoint == U'\n') { pen_x = 0.f; pen_y -= line_height; continue; }
        auto found = font.chs.find(codepoint);
        if (found == font.chs.end()) found = font.chs.find(U'?');
        if (found == font.chs.end()) continue;
        const auto& glyph = found->second;
        const int atlas_x = std::lround(glyph.uv_min.x * atlas_width);
        const int atlas_y = std::lround(glyph.uv_max.y * atlas_height);
        const int width = glyph.size.x, height = glyph.size.y;
        const auto filled = [&](int x, int y) {
            return x >= 0 && x < width && y >= 0 && y < height &&
                font.atlas.m_buffer[std::size_t(atlas_y + y) * atlas_width + atlas_x + x] >= 128;
        };
        if (atlas_x >= 0 && atlas_y >= 0 && atlas_x + width <= atlas_width && atlas_y + height <= atlas_height)
            for (int y = 0; y < height; ++y) {
                const float top = pen_y + glyph.bearing.y - y, bottom = top - 1.f;
                for (int x = 0; x < width;) {
                    if (!filled(x, y)) { ++x; continue; }
                    const int start = x;
                    while (filled(x, y)) ++x;
                    const float left = pen_x + glyph.bearing.x + start, right = left + x - start;
                    quad({left,bottom,0}, {right,bottom,0}, {right,top,0}, {left,top,0}, 1.f);
                    quad({left,bottom,-1}, {right,bottom,-1}, {right,top,-1}, {left,top,-1}, .8f);
                    quad({left,bottom,0}, {left,top,0}, {left,top,-1}, {left,bottom,-1}, .6f);
                    quad({right,bottom,0}, {right,top,0}, {right,top,-1}, {right,bottom,-1}, .7f);
                    for (int column = start; column < x; ++column) {
                        const float edge = left + column - start;
                        if (!filled(column, y - 1))
                            quad({edge,top,0}, {edge+1,top,0}, {edge+1,top,-1}, {edge,top,-1}, .85f);
                        if (!filled(column, y + 1))
                            quad({edge,bottom,0}, {edge+1,bottom,0}, {edge+1,bottom,-1}, {edge,bottom,-1}, .55f);
                    }
                }
            }
        pen_x += glyph.advance;
    }
    return vertices;
}
}

void render_system::draw_world_text(const std::shared_ptr<ecs>& registry, camera& view) {
    auto& fonts = bgui::font_manager::get_instance();
    if (!fonts.has_font("default")) return;
    const auto& font = fonts.get_font("default");
    bool drawing = false;
    shader program(shader::deferred_t{});
    GLint vao = 0, buffer = 0, previous_program = 0;
    GLboolean culling = GL_FALSE;
    registry->cada<hud>([&](std::uint32_t id) {
        const auto overlay = registry->get<hud>(id);
        if (!overlay->enabled) return;
        for (const auto& item : overlay->elements) {
            if (item->kind != "text3d" || !item->visible || item->text.empty() ||
                item->scale <= 0.f || item->color.w <= 0.f) continue;
            if (!drawing) {
                program.compilar("world_text.vert", "world_text.frag");
                glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
                culling = glIsEnabled(GL_CULL_FACE);
                glDisable(GL_CULL_FACE);
                program.use();
                drawing = true;
            }
            auto& mesh = m_text_meshes[item.get()];
            if (!mesh.vao || mesh.owner.lock() != item || mesh.text != item->text) {
                const auto vertices = text_geometry(font, item->text);
                if (!mesh.vao) {
                    glGenVertexArrays(1, &mesh.vao);
                    glGenBuffers(1, &mesh.buffer);
                }
                glBindVertexArray(mesh.vao);
                glBindBuffer(GL_ARRAY_BUFFER, mesh.buffer);
                glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
                glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
                glEnableVertexAttribArray(0);
                mesh.owner = item;
                mesh.text = item->text;
                mesh.vertices = static_cast<int>(vertices.size() / 4);
            }
            transform placement;
            placement.set_position(item->position);
            placement.set_rotation(item->rotation);
            placement.set_scale(item->world_scale);
            const float size = item->scale / std::max(1u, font.resolution);
            const auto matrix = view.projMatriz * view.viewMatrix * placement.get_world_model_matrix() *
                glm::scale(glm::mat4(1.f), glm::vec3(size, size, item->depth));
            program.setMat4("mvp", glm::value_ptr(matrix));
            program.setVec4("ink", item->color);
            glBindVertexArray(mesh.vao);
            glDrawArrays(GL_TRIANGLES, 0, mesh.vertices);
        }
    });
    if (drawing) {
        if (culling) glEnable(GL_CULL_FACE);
        glUseProgram(previous_program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
    }
}

void render_system::render(const std::shared_ptr<ecs>& reg, camera* explicit_view,
    std::uint32_t replacement_entity, const std::shared_ptr<renderer>& replacement,
    const std::shared_ptr<transform>& replacement_transform) {
    const auto start_time = std::chrono::steady_clock::now();
    m_last_camera_count = 0;
    m_last_rendered_entity_count = 0;

    if (!reg) {
        m_last_render_time_ms = 0.0;
        return;
    }

    std::erase_if(m_text_meshes, [](const auto& entry) {
        if (!entry.second.owner.expired()) return false;
        glDeleteBuffers(1, &entry.second.buffer);
        glDeleteVertexArrays(1, &entry.second.vao);
        return true;
    });

    directional_light global_direction_light{
        {-0.2f, -1.0f, -0.3f},
        bgui::vec3(0.15f),
        bgui::vec3(1.0f),
        1.0f
    };
    global_direction_light.shadows = false; // No shadow pass for the implicit fallback light.
    reg->cada<directional_light>([&](const uint32_t light_entity) {
        if (reg->has<camera>(light_entity))
            return;
        if (auto light = reg->get<directional_light>(light_entity)) {
            global_direction_light = *light;
            global_direction_light.direction = light->get_world_direction();
        }
    });

    std::vector<point_light> point_lights;
    reg->cada<point_light>([&](const uint32_t light_entity) {
        if (auto light = reg->get<point_light>(light_entity))
            point_lights.push_back(*light);
    });

    std::vector<spot_light> spot_lights;
    reg->cada<spot_light>([&](const uint32_t light_entity) {
        if (auto light = reg->get<spot_light>(light_entity)) {
            auto world_light = *light;
            world_light.position = light->get_world_position();
            world_light.direction = light->get_world_direction();
            spot_lights.push_back(std::move(world_light));
        }
    });

    using renderable = std::pair<std::shared_ptr<renderer>, std::shared_ptr<transform>>;
    std::vector<renderable> renderables;
    reg->cada<renderer, transform>([&](const uint32_t render_entity) {
        const bool replace = render_entity == replacement_entity;
        renderables.emplace_back(
            replace && replacement ? replacement : reg->get<renderer>(render_entity),
            replace && replacement_transform ? replacement_transform : reg->get<transform>(render_entity)
        );
    });

    std::unordered_map<const model*, bool> available_models;
    for (const auto& [render_component, transform_component] : renderables) {
        (void)transform_component;
        if (render_component && render_component->m_modelo &&
            !available_models.contains(render_component->m_modelo))
            available_models.emplace(render_component->m_modelo, render_component->m_modelo->source_available());
    }

    const auto draw_depth = [&](const depth_map& map, const glm::mat4& matrix, int x, int y,
                                int width, int height, const std::vector<renderable>& geometry) {
        GLint framebuffer, viewport[4], depth_function, scissor_box[4], vertex_array, array_buffer, program;
        GLboolean depth_mask;
        const bool depth_test = glIsEnabled(GL_DEPTH_TEST);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        const bool cull = glIsEnabled(GL_CULL_FACE);
        glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertex_array);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &array_buffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, map.fbo);
        glViewport(x, y, width, height);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        glDisable(GL_CULL_FACE); // Thin and double-sided surfaces can cast shadows.
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, width, height);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        shader depth_shader("shadow.vert", "shadow.frag");
        depth_shader.use();
        depth_shader.setMat4("lightSpaceMatrix", glm::value_ptr(matrix));
        for (const auto& [renderer, transform] : geometry) {
            if (!renderer || !transform || !renderer->visible || !renderer->m_modelo ||
                !available_models.at(renderer->m_modelo) || !renderer->cast_shadows) continue;
            for (auto& mesh : renderer->m_modelo->meshes) {
                if (mesh.isOverlayed()) continue;
                if (!mesh.isLoaded()) mesh.load();
                const auto& instances = mesh.instances();
                auto draw = [&](const glm::mat4& matrix) {
                    depth_shader.setMat4("model", glm::value_ptr(matrix));
                    glBindVertexArray(mesh.getVertexArray());
                    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.indices().size()), GL_UNSIGNED_INT, nullptr);
                };
                if (instances.empty()) draw(transform->get_model_matrix());
                else for (const auto& instance : instances)
                    draw(instance.get_world_model_matrix());
            }
        }
        glBindVertexArray(vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, array_buffer);
        glUseProgram(program);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glDepthFunc(depth_function);
        glDepthMask(depth_mask);
        if (!depth_test) glDisable(GL_DEPTH_TEST);
        glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        if (cull) glEnable(GL_CULL_FACE);
    };

    const auto shadow_size_for = [](int resolution) {
        GLint max_texture_size;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        return std::min(std::clamp(resolution, 128, 8192), max_texture_size);
    };

    // A 4x4 atlas keeps all 16 spotlight shadows in one texture unit.
    std::array<glm::mat4, 16> spot_spaces;
    std::array<bool, 16> spot_shadows{};
    bool has_spot_shadows = false;
    bool spot_shadows_prepared = false;
    const auto prepare_spot_shadows = [&]() {
        const bool needs_atlas = std::any_of(spot_lights.begin(), spot_lights.end(),
            [](const auto& light) { return light.shadows && light.intensity > 0.f; });
        const int atlas_size = needs_atlas ? shadow_size_for(4096) / 4 * 4 : 0;
        has_spot_shadows = needs_atlas && prepare_depth_map(m_spot_shadow, atlas_size, atlas_size);
        if (has_spot_shadows) {
            const int tile_size = m_spot_shadow.width / 4;
            for (size_t index = 0; index < spot_lights.size(); ++index) {
                const auto& light = spot_lights[index];
                if (!light.shadows || !(light.intensity > 0.f)) continue;
                const glm::vec3 position{light.position.x, light.position.y, light.position.z};
                const auto ray = glm::normalize(glm::vec3{light.direction.x, light.direction.y, light.direction.z});
                const auto up = std::abs(ray.y) > 0.99f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
                const float inner = finite_clamp(light.inner_cutoff, 12.5f, 0.f, 89.f);
                const float outer = finite_clamp(light.outer_cutoff, 17.5f, inner + 0.1f, 89.9f);
                spot_spaces[index] = glm::perspective(glm::radians(outer * 2.f), 1.f, 0.1f,
                    finite_clamp(light.shadow_distance, 50.f, 1.f, 10000.f)) *
                    glm::lookAt(position, position + ray, up);
                draw_depth(m_spot_shadow, spot_spaces[index], static_cast<int>(index % 4) * tile_size,
                    static_cast<int>(index / 4) * tile_size, tile_size, tile_size, renderables);
                spot_shadows[index] = true;
            }
        }
        spot_shadows_prepared = true;
    };

    const auto draw_camera = [&](camera* camera_component) {
        if (!camera_component) return;
        if (!spot_shadows_prepared) prepare_spot_shadows();
        ++m_last_camera_count;
        camera_component->drawFB();

        const auto view = camera_component->getViewMatrix();
        const auto projection = camera_component->obtProjectionMatrix();
        const auto frustum = extract_frustum_planes(view, projection);
        if (!camera_component->m_skybox) camera_component->m_skybox = new skybox();
        camera_component->m_skybox->draw_background(view, projection,
            camera_component->skybox_faces, camera_component->skybox_asset_root,
            camera_component->config.use_skybox,
            {camera_component->config.sky_solid_color.r, camera_component->config.sky_solid_color.g,
                camera_component->config.sky_solid_color.b},
            camera_component->config.atmosphere_zenith, camera_component->config.skybox_image);

        auto direction_light = global_direction_light;
        if (auto camera_light = (!explicit_view || camera_component->reg.lock() == reg) ?
            reg->get<directional_light>(camera_component->my_object) : nullptr) {
            direction_light = *camera_light;
            direction_light.direction = camera_light->get_world_direction();
        }

        glm::mat4 light_space{1.f};
        const glm::vec3 direction{direction_light.direction.x, direction_light.direction.y, direction_light.direction.z};
        const bool valid_direction = std::isfinite(glm::dot(direction, direction)) && glm::dot(direction, direction) > 1e-8f;
        const bool needs_shadow = direction_light.shadows && valid_direction;
        const int shadow_size = needs_shadow ? shadow_size_for(direction_light.shadow_resolution) : 0;
        const bool shadows = needs_shadow && prepare_depth_map(m_directional_shadow, shadow_size, shadow_size);
        GLint shadow_binding = 0, active_texture = GL_TEXTURE0;
        if (shadows) {
            const float distance = finite_clamp(direction_light.shadow_distance, 50.f, 1.f, 10000.f);
            const glm::vec3 center{camera_component->position.x, camera_component->position.y, camera_component->position.z};
            const auto ray = glm::normalize(direction);
            const auto up = std::abs(ray.y) > 0.99f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
            light_space = glm::ortho(-distance, distance, -distance, distance, 0.1f, distance * 4.f) *
                glm::lookAt(center - ray * (distance * 2.f), center, up);
            draw_depth(m_directional_shadow, light_space, 0, 0, shadow_size, shadow_size, renderables);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
            glActiveTexture(GL_TEXTURE15);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &shadow_binding);
            glBindTexture(GL_TEXTURE_2D, m_directional_shadow.texture);
            glActiveTexture(active_texture);
        }

        GLint spot_binding = 0, spot_active_texture = GL_TEXTURE0;
        if (has_spot_shadows) {
            glGetIntegerv(GL_ACTIVE_TEXTURE, &spot_active_texture);
            glActiveTexture(GL_TEXTURE14);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &spot_binding);
            glBindTexture(GL_TEXTURE_2D, m_spot_shadow.texture);
            glActiveTexture(spot_active_texture);
        }

        std::vector<renderable> visible_geometry;
        visible_geometry.reserve(renderables.size());
        for (const auto& [render_component, transform_component] : renderables) {
            if (!render_component || !transform_component || !render_component->m_modelo) {
                debugging::emit(debug, "render", "Renderizador ou transformação inválida");
                continue;
            }
            if (!render_component->visible || !available_models.at(render_component->m_modelo))
                continue;

            const auto [world_center, world_radius] = model_bounds_in_world_space(*render_component, *transform_component);
            if (!sphere_in_frustum(world_center, world_radius, frustum))
                continue;

            visible_geometry.emplace_back(render_component, transform_component);
        }

        const auto set_camera_uniforms = [&](shader& shader, bool receive_shadows) {
            shader.use();
            shader.setBool("shadowEnabled", shadows && receive_shadows);
            shader.setInt("shadowMap", 15);
            shader.setInt("spotShadowMap", 14);
            shader.setMat4("lightSpaceMatrix", glm::value_ptr(light_space));
            shader.setFloat("shadowBias", finite_clamp(direction_light.shadow_bias, 0.001f, 0.f, 0.1f));
            shader.setFloat("shadowStrength", finite_clamp(direction_light.shadow_strength, 1.f, 0.f, 1.f));
            shader.setMat4("view", glm::value_ptr(view));
            shader.setMat4("projection", glm::value_ptr(projection));
            shader.setVec3("viewPos", camera_component->position);
            shader.setBool("fogEnabled", camera_component->config.fog_enabled);
            shader.setVec3("fogColor", {camera_component->config.fog_color.r,
                camera_component->config.fog_color.g, camera_component->config.fog_color.b});
            shader.setFloat("fogDensity", camera_component->config.fog_density);
            shader.setVec3("dirLight.direction", direction_light.direction);
            shader.setVec3("dirLight.ambient", direction_light.ambient);
            shader.setVec3("dirLight.color", direction_light.color);
            shader.setFloat("dirLight.intensity", direction_light.intensity);
            shader.setInt("pointLightCount", static_cast<int>(point_lights.size()));

            for (size_t index = 0; index < point_lights.size(); ++index) {
                const auto prefix = "pointLights[" + std::to_string(index) + "].";
                shader.setVec3(prefix + "position", point_lights[index].position);
                shader.setVec3(prefix + "color", point_lights[index].color);
                shader.setFloat(prefix + "intensity", point_lights[index].intensity);
                shader.setFloat(prefix + "constant", point_lights[index].constant);
                shader.setFloat(prefix + "linear", point_lights[index].linear);
                shader.setFloat(prefix + "quadratic", point_lights[index].quadratic);
            }
            shader.setInt("spotLightCount", static_cast<int>(spot_lights.size()));

            for (size_t index = 0; index < spot_lights.size(); ++index) {
                const auto prefix = "spotLights[" + std::to_string(index) + "].";
                const auto& light = spot_lights[index];
                shader.setBool(prefix + "shadowEnabled", spot_shadows[index] && receive_shadows);
                if (spot_shadows[index])
                    shader.setMat4(prefix + "shadowMatrix", glm::value_ptr(spot_spaces[index]));
                shader.setFloat(prefix + "shadowBias", finite_clamp(light.shadow_bias, 0.001f, 0.f, 0.1f));
                shader.setFloat(prefix + "shadowStrength", finite_clamp(light.shadow_strength, 1.f, 0.f, 1.f));
                shader.setVec3(prefix + "position", light.position);
                shader.setVec3(prefix + "direction", light.direction);
                shader.setVec3(prefix + "color", light.color);
                shader.setFloat(prefix + "intensity", light.intensity);
                shader.setFloat(prefix + "constant", light.constant);
                shader.setFloat(prefix + "linear", light.linear);
                shader.setFloat(prefix + "quadratic", light.quadratic);
                const float inner = finite_clamp(light.inner_cutoff, 12.5f, 0.f, 89.f);
                const float outer = finite_clamp(light.outer_cutoff, 17.5f, inner + 0.1f, 89.9f);
                shader.setFloat(prefix + "innerCutoff", std::cos(glm::radians(inner)));
                shader.setFloat(prefix + "outerCutoff", std::cos(glm::radians(outer)));
            }

        };

        std::optional<material_binding> binding;
        const material* bound_material = nullptr;
        GLuint bound_program = 0;
        bool bound_receives_shadows = false;
        for (const auto& [render_component, transform_component] : visible_geometry) {
            auto& model = *render_component->m_modelo;
            auto& shader = model.getShader();
            const bool receive_shadows = render_component->receive_shadows;
            if (!bound_material || bound_program != shader.ID || bound_receives_shadows != receive_shadows)
                set_camera_uniforms(shader, receive_shadows);
            bound_receives_shadows = receive_shadows;
            shader.setMat4("model", glm::value_ptr(transform_component->get_model_matrix()));
            model.advance_animation();
            const bool standard_shader = shader.vert == "pbr.vert" && shader.frag == "pbr.frag";
            for (auto& mesh : model.meshes) {
                if (!mesh.isLoaded()) mesh.load();
                const auto& material = mesh.material_configuration();
                const bool can_batch = standard_shader && !mesh.isOverlayed() && mesh.instances().empty();
                if (can_batch && bound_material && bound_program == shader.ID &&
                    material.same_configuration(*bound_material)) {
                    mesh.draw_geometry();
                    continue;
                }

                binding.reset();
                bound_material = nullptr;
                // Only reuse material uniforms that cannot override camera or object state.
                if (can_batch && batchable_material(material)) {
                    binding.emplace(material, shader);
                    shader.setBool("instance", false);
                    bound_material = &material;
                    bound_program = shader.ID;
                    mesh.draw_geometry();
                } else {
                    mesh.draw(shader);
                }
            }
            ++m_last_rendered_entity_count;
        }
        binding.reset();

        if (has_spot_shadows) {
            glActiveTexture(GL_TEXTURE14);
            glBindTexture(GL_TEXTURE_2D, spot_binding);
            glActiveTexture(spot_active_texture);
        }
        if (shadows) {
            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D, shadow_binding);
            glActiveTexture(active_texture);
        }
        apply_camera_occlusion(*camera_component);
        draw_world_text(reg, *camera_component);
    };
    if (explicit_view) draw_camera(explicit_view);
    else reg->cada<camera>([&](const std::uint32_t entity) {
        if (m_camera_filter && !m_camera_filter(entity)) return;
        auto camera_component=reg->get<camera>(entity);
        draw_camera(camera_component.get());
    });

    m_last_render_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time
    ).count();
}
