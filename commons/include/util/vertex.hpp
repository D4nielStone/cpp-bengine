
/**
 * @file vertex.hpp
 */

#pragma once
#include "commons_namespace.hpp"
#include <utils/vec.hpp>

namespace COMMONS_NS {
	struct vertex {
	    vertex() = default;
	    vertex(const fvec3& p, const fvec3& n, const fvec2& uv) : position(p), normal(n), uvcoords(uv) {}
		fvec3 position, normal;
		fvec2 uvcoords;
	};
}
