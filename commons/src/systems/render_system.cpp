#include "systems/render_system.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "debugging/debug.hpp"
#include "glad.h"

#include <memory>
#include <utility>
#include <vector>

using namespace COMMONS_NS;

namespace {
float finite_clamp(float value, float fallback, float low, float high) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}
}

render_system::~render_system() {
    // Systems may outlive the window during application shutdown.
    if (glad_glGetString && glGetString(GL_VERSION)) {
        if (m_shadow_texture) glDeleteTextures(1, &m_shadow_texture);
        if (m_shadow_fbo) glDeleteFramebuffers(1, &m_shadow_fbo);
    }
}

bool render_system::prepare_shadow_map(int resolution) {
    resolution = std::clamp(resolution, 128, 8192);
    if (m_shadow_fbo && m_shadow_resolution == resolution) return true;
    GLint max_texture_size;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
    resolution = std::min(resolution, max_texture_size);
    if (m_shadow_fbo && m_shadow_resolution == resolution) return true;
    GLint framebuffer, read_framebuffer, texture;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_framebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    if (!m_shadow_fbo) glGenFramebuffers(1, &m_shadow_fbo);
    if (!m_shadow_texture) glGenTextures(1, &m_shadow_texture);
    glBindTexture(GL_TEXTURE_2D, m_shadow_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadow_fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_shadow_texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (!complete) {
        glDeleteTextures(1, &m_shadow_texture);
        glDeleteFramebuffers(1, &m_shadow_fbo);
        m_shadow_texture = m_shadow_fbo = 0;
    }
    m_shadow_resolution = complete ? resolution : 0;
    return complete;
}

void render_system::setup(const std::shared_ptr<ecs>&) {
}

void render_system::update(const std::shared_ptr<ecs>& reg) {
    const auto start_time = std::chrono::steady_clock::now();
    m_last_camera_count = 0;
    m_last_rendered_entity_count = 0;

    if (!reg) {
        m_last_render_time_ms = 0.0;
        return;
    }

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
            const glm::vec3 direction{
                world_light.direction.x, world_light.direction.y, world_light.direction.z
            };
            const float length_squared = glm::dot(direction, direction);
            if (std::isfinite(length_squared) && length_squared > 1e-8f)
                spot_lights.push_back(std::move(world_light));
        }
    });

    std::vector<std::pair<std::shared_ptr<renderer>, std::shared_ptr<transform>>> renderables;
    reg->cada<renderer, transform>([&](const uint32_t render_entity) {
        renderables.emplace_back(
            reg->get<renderer>(render_entity),
            reg->get<transform>(render_entity)
        );
    });

    reg->cada<camera>([&](const uint32_t entity) {
        if (m_camera_filter && !m_camera_filter(entity))
            return;
        auto camera_component = reg->get<camera>(entity);
        if (!camera_component) {
            return;
        }

        ++m_last_camera_count;
        camera_component->drawFB();

        const auto view = camera_component->getViewMatrix();
        const auto projection = camera_component->obtProjectionMatrix();
        if (!camera_component->m_skybox) camera_component->m_skybox = new skybox();
        camera_component->m_skybox->draw_background(view, projection,
            camera_component->skybox_faces, camera_component->skybox_asset_root,
            camera_component->m_use_skybox,
            {camera_component->ceu.r, camera_component->ceu.g, camera_component->ceu.b},
            camera_component->atmosphere_zenith, camera_component->skybox_image);

        auto direction_light = global_direction_light;
        if (auto camera_light = reg->get<directional_light>(camera_component->my_object)) {
            direction_light = *camera_light;
            direction_light.direction = camera_light->get_world_direction();
        }

        glm::mat4 light_space{1.f};
        const glm::vec3 direction{direction_light.direction.x, direction_light.direction.y, direction_light.direction.z};
        const bool valid_direction = std::isfinite(glm::dot(direction, direction)) && glm::dot(direction, direction) > 1e-8f;
        const bool shadows = direction_light.shadows && valid_direction && prepare_shadow_map(direction_light.shadow_resolution);
        GLint shadow_binding = 0, active_texture = GL_TEXTURE0;
        if (shadows) {
            const float distance = finite_clamp(direction_light.shadow_distance, 50.f, 1.f, 10000.f);
            const glm::vec3 center{camera_component->position.x, camera_component->position.y, camera_component->position.z};
            const auto ray = glm::normalize(direction);
            const auto up = std::abs(ray.y) > 0.99f ? glm::vec3{0, 0, 1} : glm::vec3{0, 1, 0};
            light_space = glm::ortho(-distance, distance, -distance, distance, 0.1f, distance * 4.f) *
                glm::lookAt(center - ray * (distance * 2.f), center, up);
            GLint framebuffer, viewport[4], depth_function;
            GLboolean depth_mask;
            const bool depth_test = glIsEnabled(GL_DEPTH_TEST);
            const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
            const bool cull = glIsEnabled(GL_CULL_FACE);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
            glGetIntegerv(GL_VIEWPORT, viewport);
            glGetIntegerv(GL_DEPTH_FUNC, &depth_function);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_shadow_fbo);
            glViewport(0, 0, m_shadow_resolution, m_shadow_resolution);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_CULL_FACE); // Thin and double-sided surfaces can cast shadows.
            glClear(GL_DEPTH_BUFFER_BIT);
            shader depth_shader("shadow.vert", "shadow.frag");
            depth_shader.use();
            depth_shader.setMat4("lightSpaceMatrix", glm::value_ptr(light_space));
            for (const auto& [renderer, transform] : renderables) {
                if (!renderer || !transform || !renderer->visible || !renderer->cast_shadows || !renderer->m_modelo) continue;
                for (auto& mesh : renderer->m_modelo->meshes) {
                    if (mesh.isOverlayed()) continue;
                    if (!mesh.isLoaded()) mesh.load();
                    const auto& instances = mesh.instances();
                    auto draw = [&](const glm::mat4& matrix) {
                        depth_shader.setMat4("model", glm::value_ptr(matrix));
                        glBindVertexArray(mesh.getVertexArray());
                        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.getIndices().size()), GL_UNSIGNED_INT, nullptr);
                    };
                    if (instances.empty()) draw(transform->get_model_matrix());
                    else for (const auto& instance : instances) draw(instance.get_world_model_matrix());
                }
            }
            glBindVertexArray(0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
            glDepthFunc(depth_function);
            glDepthMask(depth_mask);
            if (!depth_test) glDisable(GL_DEPTH_TEST);
            if (scissor) glEnable(GL_SCISSOR_TEST);
            if (cull) glEnable(GL_CULL_FACE);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
            glActiveTexture(GL_TEXTURE15);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &shadow_binding);
            glBindTexture(GL_TEXTURE_2D, m_shadow_texture);
            glActiveTexture(active_texture);
        }

        for (const auto& [render_component, transform_component] : renderables) {
            if (!render_component || !transform_component || !render_component->m_modelo) {
                debugging::emit(debug, "render", "Renderizador ou transformação inválida");
                continue;
            }
            if (!render_component->visible)
                continue;

            auto& shader = render_component->m_modelo->getShader();
            shader.use();
            shader.setBool("shadowEnabled", shadows && render_component->receive_shadows);
            shader.setInt("shadowMap", 15);
            shader.setMat4("lightSpaceMatrix", glm::value_ptr(light_space));
            shader.setFloat("shadowBias", finite_clamp(direction_light.shadow_bias, 0.001f, 0.f, 0.1f));
            shader.setFloat("shadowStrength", finite_clamp(direction_light.shadow_strength, 1.f, 0.f, 1.f));
            shader.setMat4("view", glm::value_ptr(view));
            shader.setMat4("projection", glm::value_ptr(projection));
            shader.setMat4("model", glm::value_ptr(transform_component->get_model_matrix()));
            shader.setVec3("viewPos", camera_component->position);
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

            render_component->m_modelo->draw();
            ++m_last_rendered_entity_count;
        }

        if (shadows) {
            glActiveTexture(GL_TEXTURE15);
            glBindTexture(GL_TEXTURE_2D, shadow_binding);
            glActiveTexture(active_texture);
        }
    });

    m_last_render_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time
    ).count();
}
