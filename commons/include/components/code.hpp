/**
 * @file code.hpp
 * @brief Componente de código para entities ECS.
 *
 * @see code.cpp
 */

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <array>
#include <cstdint>
#include <variant>
#include <lua.hpp>
#include <sol/sol.hpp>
#include <rapidjson/rapidjson.h>
#include "commons_namespace.hpp"
#include "component.hpp"

namespace COMMONS_NS {
    struct physics_system;
    // Metadata is read without executing Lua. Images are project-relative asset paths.
    struct script_property {
        using value_type = std::variant<double, bool, std::string, std::array<float, 4>, std::uint32_t>;
        std::string name;
        std::string type;
        value_type value;
        bool operator==(const script_property&) const = default;
    };

	struct code : component {
		static constexpr mask mask = COMPONENTE_CODIGO;
		std::string file;
        // The first script keeps the legacy public fields; remaining entries own independent Lua states.
        std::vector<std::shared_ptr<code>> scripts;

        std::string asset_root; // Resolved project directory; not serialized.
        std::vector<script_property> properties;
        bool load_properties(std::string& error);
        bool set_property(const std::string& name, const script_property::value_type& value);

		std::string arquivoCompleto;
        sol::state estado_lua;
        sol::function f_setup, f_update;

		/**
		* @brief carrega script lua
		*/
		code(const std::string& file);
		/**
		* @brief limpa recursos
		*/
		~code();
		code() = default;
        bool analyze(const rapidjson::Value&) override;
        bool serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator) const override;
		void setup();
        void setup(const std::weak_ptr<physics_system>& physics_world);
		void update();
	};
}
