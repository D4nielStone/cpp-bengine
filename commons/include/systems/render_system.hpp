#pragma once
#include "system.hpp"
#include <memory>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace COMMONS_NS {
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
      std::function<bool(std::uint32_t)> m_camera_filter;
      struct shadow_map {
        unsigned int fbo{0}, texture{0};
        int resolution{0};
      };
      shadow_map m_directional_shadow, m_spot_shadow;
      bool prepare_shadow_map(shadow_map& map, int resolution);
      double m_last_render_time_ms{0.0};
      std::size_t m_last_camera_count{0};
      std::size_t m_last_rendered_entity_count{0};
	};
}
/** @see render_system.cpp */
