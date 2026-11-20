#pragma once
#include "system.hpp"
#include "profiling/gpu_timer.hpp"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <chrono>
#include <array>
#include <glm/glm.hpp>

namespace COMMONS_NS {
    class camera;
    class renderer;
    struct directional_light;
    class transform;
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
      std::chrono::steady_clock::time_point m_shader_time_start{std::chrono::steady_clock::now()};
      struct camera_buffers {
        unsigned int depth{}, mask{}, mask_fbo{}, color{};
        int width{}, height{};
        int depth_width{}, depth_height{}, mask_width{}, mask_height{}, color_width{}, color_height{};
        std::uint64_t last_use{};
      };
      std::array<camera_buffers, 3> m_camera_buffers{};
      std::uint64_t m_camera_buffer_use{};
      unsigned int m_fullscreen_vao{};
      camera_buffers& buffers_for_view(int width, int height);
      bool capture_camera_depth(camera_buffers& buffers);
      void apply_camera_occlusion(const camera& view, camera_buffers& buffers);
      void apply_camera_shader(const camera& camera, const glm::mat4& view,
          const directional_light& light, const glm::mat4& light_space, bool shadows,
          bool has_depth, camera_buffers& buffers);
      bool prepare_depth_map(depth_map& map, int width, int height);
      Bubble::gpu_timer m_profile_gpu;
      Bubble::gpu_timer m_profile_geometry{"Geometry and lighting"};
      Bubble::gpu_timer m_profile_shadows{"Shadows"};
      Bubble::gpu_timer m_profile_ao{"SSAO/HBAO"};
      Bubble::gpu_timer m_profile_post{"Post processing"};
      double m_last_render_time_ms{0.0};
      std::size_t m_last_camera_count{0};
      std::size_t m_last_rendered_entity_count{0};
	};
}
/** @see render_system.cpp */
