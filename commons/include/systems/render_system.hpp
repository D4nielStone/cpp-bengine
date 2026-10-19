#pragma once
#include "system.hpp"
#include <memory>
#include <cstddef>

namespace COMMONS_NS {
  /**
   * @brief Classe responsável por gerenciar o sistema de renderização do jogo.
   * @brief Atualiza os framebuffers das câmeras, configura as luzes e renderiza os objetos na tela.
   */
	class render_system : public system {
	  public:
	    render_system() = default;
		  void setup(const std::shared_ptr<ecs>&) override;
      void update(const std::shared_ptr<ecs>&) override;

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
      double m_last_render_time_ms{0.0};
      std::size_t m_last_camera_count{0};
      std::size_t m_last_rendered_entity_count{0};
	};
}
/** @see render_system.cpp */
