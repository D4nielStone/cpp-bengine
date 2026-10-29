/** @copyright
 * MIT License
 * Copyright (c) 2025 Daniel Oliveira
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * @file physics.hpp
 */

#pragma once
#include "commons_namespace.hpp"
#include "loaders/model.hpp"
#include "component.hpp"
#include <bullet/btBulletDynamicsCommon.h>
#include <rapidjson/document.h>

namespace COMMONS_NS {
    class transform;
    struct physics : component {
        static constexpr mask mask { COMPONENTE_FISICA };

        enum class shapes : uint8_t {
            box_shape   ,
            forma_capsula ,
            forma_esfera  ,
            forma_cilindro,
            forma_cone    ,
            forma_plano   ,
            mesh_shape
        };

        enum class body_type { rigid, character, static_body };
        body_type type{body_type::rigid};
        bgui::vec3 collision_size{1.f, 1.f, 1.f};
        bgui::vec3 collision_offset{0.f, 0.f, 0.f};
        bool fit_to_model{true};
        bgui::vec3 collision_rotation{0.f, 0.f, 0.f};
        btTransform synchronize_transform(const transform& value, model* geometry = nullptr);
        bool collision_enabled{true};
        bool trigger{false};
        void set_body_type(body_type value);
        void configure_collision(shapes shape, const bgui::vec3& size,
            const bgui::vec3& offset, bool enabled, bool sensor);
        void refresh_collision(const bgui::vec3& entity_scale);
        std::uint64_t collision_revision{0};
        btCompoundShape* collision_shape{nullptr};
        shapes e_forma;
        physics(bool is_static = false, const shapes m_forma = shapes::box_shape);
        virtual ~physics();

        void setModelo(model*);
        void setForca(const bgui::vec3& vector);
        void setVelocidade(const bgui::vec3& vector);
        void setFatorLinear(const bgui::vec3& fator);
        void setFatorAngular(const bgui::vec3& fator);
        void setFriccao(const float fator);
        void setRestituicao(const float fator);
        void setRaioCCD(const float fator);
        void setMass(float mass);
        bgui::vec3 getVelocidade() const;
        bool analyze(const rapidjson::Value&) override;
        bool serialize(
            rapidjson::Value& value,
            rapidjson::Document::AllocatorType& allocator
        ) const override;

        float m_massa{1.f};
        float friction{0.5f};
        float restitution{0.f};
        model* m_modelo{ nullptr };
        bgui::vec3 m_bounds_min{0.f}, m_bounds_max{0.f};
        bool m_has_model_bounds{false};
        btCollisionShape* m_forma{ nullptr };
        btDefaultMotionState* m_estado_de_movimento{ nullptr };
        btRigidBody* m_rigid_body{ nullptr };
    };
}
