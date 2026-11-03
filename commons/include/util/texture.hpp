/** @copyright
MIT License
Copyright (c) 2025 Daniel Oliveira

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
/**
 * @file texture.hpp
 */

#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
namespace bgui { struct texture; }

namespace COMMONS_NS {
    enum class texture_filter { original, bilinear, trilinear };

    struct texture {
        texture_filter filtering{texture_filter::original};
        unsigned int id{0};
        std::string path{""};
        std::shared_ptr<bgui::texture> embedded;
        std::shared_ptr<const std::vector<unsigned char>> imported_image;
        std::uint64_t imported_image_revision{0};

        texture() = default;
        texture(unsigned int id, std::string path) : id(id), path(path) {}
        // Call after preparing/changing embedded CPU pixels, before rendering.
        void update_embedded_revision();
        void bind(int slot) const;

        bool operator==(const texture& other) const {
            return id == other.id && path == other.path;
        }
    };
}
