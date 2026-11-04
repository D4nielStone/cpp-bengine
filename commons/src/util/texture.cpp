#include <glad.h>
#include "commons_namespace.hpp"
#include "util/texture.hpp"
#include <os/asset_manager.hpp>
#include <os/resource_files.hpp>
#include <bgui_backend_gl3.hpp>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
#include <charconv>

namespace {
std::uint64_t pixel_revision(const std::vector<unsigned char>& pixels) {
    std::uint64_t revision = 14695981039346656037ull;
    for (const unsigned char byte : pixels) {
        revision ^= byte;
        revision *= 1099511628211ull;
    }
    return revision ? revision : 1;
}
}

std::filesystem::path commons::texture_resolution_path(const std::filesystem::path& image) {
    auto path = image;
    return path += ".texture-resolution";
}

int commons::texture_resolution_percent(const std::filesystem::path& image) {
    const auto path = texture_resolution_path(image);
    if (bgui::resource_files::exists(path)) {
        const auto bytes = bgui::resource_files::read(path);
        if (!bytes.empty()) {
            const auto* start = reinterpret_cast<const char*>(bytes.data());
            const auto* end = start + bytes.size();
            int value{};
            const auto parsed = std::from_chars(start, end, value);
            if (parsed.ec == std::errc{} && parsed.ptr == end && value >= 1 && value <= 100)
                return value;
        }
    }
    return default_texture_resolution_percent;
}

bgui::texture commons::resize_texture(const bgui::texture& source, int percent) {
    percent = std::clamp(percent, 1, 100);
    const int width = static_cast<int>(source.m_size[0]);
    const int height = static_cast<int>(source.m_size[1]);
    if (percent >= 100 || width <= 0 || height <= 0 || source.m_buffer.empty())
        return source;

    const int out_width = std::max(1, static_cast<int>((std::int64_t(width) * percent + 50) / 100));
    const int out_height = std::max(1, static_cast<int>((std::int64_t(height) * percent + 50) / 100));
    if (out_width == width && out_height == height) return source;
    const auto channels = source.m_use_red_channel ? 1u : source.m_has_alpha ? 4u : 3u;
    if (source.m_buffer.size() != std::size_t(width) * height * channels) return source;

    auto result = source;
    result.m_size = {float(out_width), float(out_height)};
    result.m_buffer.resize(std::size_t(out_width) * out_height * channels);
    for (int y = 0; y < out_height; ++y) {
        const auto sy = std::int64_t(y) * height / out_height;
        const auto ey = (std::int64_t(y + 1) * height + out_height - 1) / out_height;
        for (int x = 0; x < out_width; ++x) {
            const auto sx = std::int64_t(x) * width / out_width;
            const auto ex = (std::int64_t(x + 1) * width + out_width - 1) / out_width;
            std::uint64_t sums[4]{};
            for (auto row = sy; row < ey; ++row)
                for (auto column = sx; column < ex; ++column)
                    for (unsigned c = 0; c < channels; ++c)
                        sums[c] += source.m_buffer[(row * width + column) * channels + c];
            const auto count = (ey - sy) * (ex - sx);
            for (unsigned c = 0; c < channels; ++c)
                result.m_buffer[(std::size_t(y) * out_width + x) * channels + c] =
                    static_cast<unsigned char>((sums[c] + count / 2) / count);
        }
    }
    result.m_revision = pixel_revision(result.m_buffer);
    return result;
}

void commons::texture::update_embedded_revision() {
    m_resized.reset();
    if (!embedded) return;
    embedded->m_revision = pixel_revision(embedded->m_buffer);
}

void commons::texture::bind(int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    unsigned int texture_id = id;
    if (!texture_id && (embedded || !path.empty())) {
        if (!m_resized || m_resized_path != path) {
            if (embedded) {
                m_resized = std::make_shared<bgui::texture>(resize_texture(*embedded, default_texture_resolution_percent));
            } else {
                auto& assets = bgui::asset_manager::get_instance();
                const auto& resolved_path = assets.resolve_asset_path(path);
                if (!bgui::resource_files::exists(resolved_path))
                    throw std::runtime_error("[Texture] Asset source is missing: " + path);
                m_resized = std::make_shared<bgui::texture>(
                    resize_texture(assets.load_texture(path), texture_resolution_percent(resolved_path)));
            }
            m_resized_path = path;
        }
        texture_id = bgui::gl3_get_texture(*m_resized);
    }
    glBindTexture(GL_TEXTURE_2D, texture_id);
}
