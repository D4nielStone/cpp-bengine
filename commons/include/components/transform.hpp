#pragma once
#include "commons_namespace.hpp"
#include "component.hpp"
#include "utils/vec.hpp"

#define GLM_ENABLE_EXPERIMENTAL

#include <glm/ext/vector_float3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <utils/vec.hpp>

namespace COMMONS_NS {
	class transform : public component {
    public:
		fvec3 up, position, rotation, scale;
		fvec3* target;
		bool m_usar_target, m_target_novo{ false };
		glm::mat4 matrizmodelo;
		static constexpr mask mask = COMPONENTE_TRANSFORMACAO;
        ~transform();
        transform(const fvec3& p = {0.f, 0.f, 0.f},
			const fvec3& r = {0.f, 0.f, 0.f},
			const fvec3& e = {1.f, 1.f, 1.f});

        bool analyze(const rapidjson::Value&) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override;

        glm::mat4 get_model_matrix();
        fvec3 get_position() const;
        fvec3 get_scale() const;
        fvec3 get_rotation() const;
        fvec3 get_target() const;
        fvec3 get_up() const;
        bool is_using_target() const;
        void set_model_matrix(const glm::mat4&);
        void set_up(const fvec3&);
        void set_position(const fvec3&);
        void set_scale(const fvec3&);
        void set_rotation(const fvec3&);
        void set_rotation(const fvector_type4&);
        void move(const fvec3&);
        void rotate(const fvec3&);
        void apply_scale(const fvec3&);
		void look_at_entity(const uint32_t& ent);
		void look_at_vector(const fvec3& pos);

		transform& operator=(const transform& tr);
	};
}
