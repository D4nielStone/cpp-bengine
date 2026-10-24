#pragma once
#include "commons_namespace.hpp"
#include "component.hpp"
#include "utils/vec.hpp"

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <utils/vec.hpp>

namespace COMMONS_NS {
	class transform : public component {
    public:
		bgui::vec3 up, position, rotation, scale;
		uint32_t parent_id{0};
		bgui::vec3* target;
		bool m_usar_target, m_target_novo{ false };
		glm::mat4 matrizmodelo;
		static constexpr mask mask = COMPONENTE_TRANSFORMACAO;
        ~transform();
        transform(const bgui::vec3& p = {0.f, 0.f, 0.f},
			const bgui::vec3& r = {0.f, 0.f, 0.f},
			const bgui::vec3& e = {1.f, 1.f, 1.f});

        bool analyze(const rapidjson::Value&) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override;

        glm::mat4 get_model_matrix();
        glm::mat4 get_world_model_matrix() const;
        bgui::vec3 get_position() const;
        bgui::vec3 get_world_position() const;
        bgui::vec3 get_scale() const;
        bgui::vec3 get_rotation() const;
        bgui::vec3 get_world_rotation() const;
        bgui::vec3 get_target() const;
        bgui::vec3 get_up() const;
        bool is_using_target() const;
        void set_model_matrix(const glm::mat4&);
        void set_up(const bgui::vec3&);
        void set_position(const bgui::vec3&);
        void set_world_position(const bgui::vec3&);
        void set_scale(const bgui::vec3&);
        void set_rotation(const bgui::vec3&);
        void set_world_rotation(const bgui::vec3&);
        bool set_parent(uint32_t parent, bool preserve_world = true);
        void move(const bgui::vec3&);
        void rotate(const bgui::vec3&);
        void apply_scale(const bgui::vec3&);
		void look_at_entity(const uint32_t& ent);
		void look_at_vector(const bgui::vec3& pos);

		transform& operator=(const transform& tr);
	};
}
