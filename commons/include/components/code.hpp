/**
 * @file code.hpp
 * @brief Componente de código para entities ECS.
 *
 * @see code.cpp
 */

#pragma once
#include <string>
#include <lua.hpp>
#include <sol/sol.hpp>
#include <rapidjson/rapidjson.h>
#include "commons_namespace.hpp"
#include "component.hpp"

namespace COMMONS_NS {
	struct code : component {
		static constexpr mask mask = COMPONENTE_CODIGO;
		std::string file;

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
		void update();
	};
}
