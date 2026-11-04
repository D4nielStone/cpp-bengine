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
 * @file code_system.hpp
 */

#pragma once
#include "system.hpp"
#include "components/code.hpp"

#include <chrono>
#include <unordered_map>

namespace COMMONS_NS {
	struct code_system : system {
    	code_system() = default;
        explicit code_system(const std::shared_ptr<physics_system>& physics_world)
            : m_physics_world(physics_world) {}
    	~code_system();
        void update(const std::shared_ptr<ecs>&) override;
        void setup(const std::shared_ptr<ecs>&) override;

    private:
        std::weak_ptr<physics_system> m_physics_world;
        struct script_state {
            std::weak_ptr<code> instance;
            bool failed{};
        };
        script_state& initialize(const std::shared_ptr<code>& script, std::uint32_t id);
        std::unordered_map<const code*, script_state> m_scripts;
        std::chrono::steady_clock::time_point m_last_update{};
    };
}
/** @see code_system.cpp */
