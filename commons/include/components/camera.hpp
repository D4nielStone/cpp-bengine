#pragma once
#include <array>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <memory>
#include <string>
#include "commons_namespace.hpp"
#include "component.hpp"
#include "transform.hpp"
#include "util/skybox.hpp"
#include "util/color.hpp"
#include "util/ray.hpp"

namespace COMMONS_NS {
	struct camera_config {
		bgui::vec3 atmosphere_zenith{0.08f, 0.25f, 0.55f};
		color sky_solid_color		{0.43F, 0.78F, 0.86F, 1.0F};
        std::string skybox_image;
		bool use_skybox		{ false };
		bool fog_enabled	{ false };
		color fog_color		{ 0.43F, 0.78F, 0.86F, 1.0F };
		float fog_density	{ 0.01f };
		float fov			{ 75.f };
		float aspecto		{ 0.f };
		float corte_curto	{ 0.1f };
		float corte_longo	{ 300.f };
		float scale			{ 5.f };

		float left{0.f}, top{0.f}, bottom{0.f}, right_limit{0.f};

		bool flag_orth		{ false };
	};
	/**
	 * @class camera
	 * @brief component que facilita a manipuacao da matriz de visualizacao e projeco
	 */
	class camera : public component {
    public:
        skybox* m_skybox{nullptr};
        std::array<std::string, 6> skybox_faces{};
        std::string skybox_asset_root; // Resolved project directory, not serialized.

		bgui::vec3 forward, right, up, position;
		transform* m_transform{ nullptr };

		glm::mat4 viewMatrix = glm::mat4(1.f), projMatriz = glm::mat4(1.f);

		unsigned int fbo, texture, rbo;

		camera_config config{};
		bool flag_fb		{ false };

		static constexpr mask mask = COMPONENTE_CAM;

		bgui::vec2i* viewport_ptr{ nullptr };
		bgui::vec2i viewportFBO{ 800, 600 };

		void drawFB();
        bool analyze(const rapidjson::Value&) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override;

        ~camera();
        void release_graphics_resources() override;
		camera(const bool ortho = false);

		void createFB();
		void deleteFB();
        unsigned int framebuffer_texture() const { return texture; }
        // Apply UI resize requests when the next camera render starts.
        void request_framebuffer_size(const bgui::vec2i& size);
    private:
        bgui::vec2i m_requested_framebuffer_size{0, 0};
    public:
		glm::mat4 getViewMatrix();
		void viewport(const bgui::vec2i& viewp);

		glm::mat4 obtProjectionMatrix();

		ray point_to_ray(const bgui::vec2& screenPoint) const;
		bgui::vec3 telaParaMundo(const bgui::vec2& screenPoint, float profundidade) const;
        bgui::vec2i worldParaTela(const bgui::vec3& worldPos);

		void move(const bgui::vec3& pos);
	};
}
