#pragma once
#include "system.hpp"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>
#include <unordered_map>

namespace COMMONS_NS {
    class camera;
    class renderer;
    class transform;
    struct hud_element;
  /**
   * @brief Classe responsável por gerenciar o sistema de renderização do jogo.
   * @brief Atualiza os framebuffers das câmeras, configura as luzes e renderiza os objetos na tela.
   */
	class render_system : public system {
	  public:
	    render_system() = default;
      ~render_system() override;
      render_system(const render_system&) = delete;
      render_system& operator=(const render_system&) = delete;
		  void setup(const std::shared_ptr<ecs>&) override;
      void update(const std::shared_ptr<ecs>&) override;

      // Render an explicit camera without inserting editor entities into the scene.
      void render_view(const std::shared_ptr<ecs>& registry, camera& view,
          std::uint32_t replacement_entity = 0, std::shared_ptr<renderer> replacement = {});

      void render_view(const std::shared_ptr<ecs>& registry, camera& view,
          std::uint32_t replacement_entity, std::shared_ptr<renderer> replacement,
          const std::shared_ptr<transform>& replacement_transform);

      // An empty filter preserves rendering to every camera.
      void set_camera_filter(std::function<bool(std::uint32_t)> filter) {
        m_camera_filter = std::move(filter);
      }

      double last_render_time_ms() const noexcept {
        return m_last_render_time_ms;
      }

      std::size_t last_camera_count() const noexcept {
        return m_last_camera_count;
      }

      std::size_t last_rendered_entity_count() const noexcept {
        return m_last_rendered_entity_count;
      }

    private:
      void render(const std::shared_ptr<ecs>&, camera*, std::uint32_t,
          const std::shared_ptr<renderer>&, const std::shared_ptr<transform>& = {});
      std::function<bool(std::uint32_t)> m_camera_filter;
      struct depth_map {
        unsigned int fbo{0}, texture{0};
        int width{0}, height{0};
      };
      depth_map m_directional_shadow, m_spot_shadow;
      unsigned int m_occlusion_depth{0}, m_occlusion_vao{0};
      int m_occlusion_width{0}, m_occlusion_height{0};
      void apply_camera_occlusion(const camera& view);
      struct text_mesh {
        std::weak_ptr<hud_element> owner;
        std::string text;
        unsigned int vao{0}, buffer{0};
        int vertices{0};
      };
      std::unordered_map<const hud_element*, text_mesh> m_text_meshes;
      void draw_world_text(const std::shared_ptr<ecs>&, camera&);
      bool prepare_depth_map(depth_map& map, int width, int height);
      double m_last_render_time_ms{0.0};
      std::size_t m_last_camera_count{0};
      std::size_t m_last_rendered_entity_count{0};
	};
}
/** @see render_system.cpp */
