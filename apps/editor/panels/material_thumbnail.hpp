#pragma once

#include "../systems/camera_frame.hpp"
#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "../systems/render_viewport.hpp"
#include "util/pbr_material.hpp"
#include <glad.h>
#include <utils/texture.hpp>

namespace editor::ui {

class material_thumbnail_renderer {
public:
    static bool available() { return glad_glGetString && glGetString(GL_VERSION); }

    bgui::texture render(const std::filesystem::path& path, const commons::material& material) {
        if (!m_registry) initialize();
        auto& mesh = m_sphere->m_modelo->meshes.front();
        auto surface = material;
        surface.set_uniform("instance", false);
        if (!surface.uniforms.contains("material.albedo"))
            surface.set_uniform("material.albedo", commons::color{1.f, 1.f, 1.f, 1.f});
        for (const auto& factor : commons::pbr_factors) {
            const auto name = std::string("material.") + factor.name;
            if (!surface.uniforms.contains(name)) surface.set_uniform(name, factor.fallback);
        }
        std::erase_if(surface.textures, [](const auto& entry) {
            return std::none_of(commons::pbr_maps.begin(), commons::pbr_maps.end(),
                [&entry](const auto& map) { return entry.first == map.name; });
        });
        for (const auto& map : commons::pbr_maps) {
            const auto name = std::string("use_") + map.name;
            if (!surface.uniforms.contains(name)) surface.set_uniform(name, false);
        }
        mesh.setMaterial(surface);
        const auto frame = render_viewport(m_renderer, m_registry, m_camera, extent,
            extent, m_sphere->my_object);
        if (!frame.texture) return {};

        bgui::texture image;
        image.m_path = "material-preview:" + path.generic_string();
        image.m_size = {extent, extent};
        image.m_generate_mipmap = false;
        image.m_buffer = read_camera_frame(m_camera, extent, extent);
        return image;
    }

private:
    static constexpr int extent = 96;
    std::shared_ptr<commons::ecs> m_registry;
    std::shared_ptr<commons::renderer> m_sphere;
    commons::camera m_camera;
    commons::render_system m_renderer;

    void initialize() {
        auto registry = std::make_shared<commons::ecs>();
        const auto asset = std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / "sphere.bmodel";
        auto model = std::make_unique<commons::model>(asset.string());
        if (model->meshes.empty()) throw std::runtime_error("Modelo da esfera indisponível.");
        auto entity = registry->create();
        registry->add<commons::renderer>(entity, model.get());
        model.release();
        registry->get<commons::transform>(entity.id)->set_rotation({-90.f, 0.f, 0.f});
        auto sphere = registry->get<commons::renderer>(entity.id);
        sphere->cast_shadows = false;

        auto light = registry->create();
        registry->add<commons::directional_light>(light, bgui::vec3{-.5f, -.8f, -1.f},
            bgui::vec3(.35f), bgui::vec3(1.f), 2.f);
        registry->get<commons::directional_light>(light.id)->shadows = false;
        m_camera.config.fov = 38.f;
        m_camera.config.sky_solid_color = {.065f, .07f, .085f, 1.f};
        m_camera.position = {0.f, 0.f, 3.6f};
        m_camera.viewMatrix = glm::lookAt(glm::vec3{0.f, 0.f, 3.6f},
            glm::vec3{0.f}, glm::vec3{0.f, 1.f, 0.f});
        m_sphere = std::move(sphere);
        m_registry = std::move(registry);
    }
};
}
