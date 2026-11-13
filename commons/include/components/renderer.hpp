#pragma once
#include <rapidjson/rapidjson.h>
#include "commons_namespace.hpp"
#include "util/mesh.hpp"
#include "loaders/model.hpp"
#include "component.hpp"

namespace COMMONS_NS {
	class renderer : public component {
    public:
        model* m_modelo{nullptr};
	    bool visible{true};
        bool cast_shadows{true};
        bool receive_shadows{true};
		static constexpr mask mask = COMPONENTE_RENDER;
		renderer(model* mesh) : m_modelo(mesh) {
		}
        bool analyze(const rapidjson::Value& value) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override
        {
            if (!m_modelo)
                return false;
            value.AddMember("cast_shadows", cast_shadows, allocator);
            value.AddMember("receive_shadows", receive_shadows, allocator);
            value.AddMember("model", rapidjson::Value(m_modelo->get_source_path().c_str(), allocator), allocator);
            return serialize_rig_pose(value, allocator);
        };
        std::vector<animation_property> animation_properties() const override;
        std::optional<double> read_animation_property(const std::string& path) const override;
        bool write_animation_property(const std::string& path, double value) override;
        bool analyze_rig_pose(const rapidjson::Value& value);
        bool serialize_rig_pose(rapidjson::Value&, rapidjson::Document::AllocatorType&) const;

		renderer(const char* m_directory ) : m_modelo(new model(m_directory)) {};
        void release_graphics_resources() override {
            if (m_modelo)
                for (auto& mesh : m_modelo->meshes)
                    mesh.unload();
        }
		~renderer()
		{
            if (m_modelo) {
                for(auto& mesh : m_modelo->meshes)
                    mesh.unload();
                delete m_modelo;
			}
		}
        renderer() = default;
	};
}
