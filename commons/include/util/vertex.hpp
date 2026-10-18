
/**
 * @file vertex.hpp
 */

#pragma once
#include "commons_namespace.hpp"
#include <utils/vec.hpp>

namespace COMMONS_NS {
	struct vertex {
	    vertex() = default;
	    vertex(const bgui::vec3& p, const bgui::vec3& n, const bgui::vec2& uv) : position(p), normal(n), uvcoords(uv) {}
		bgui::vec3 position, normal;
		bgui::vec2 uvcoords;
	};
}
