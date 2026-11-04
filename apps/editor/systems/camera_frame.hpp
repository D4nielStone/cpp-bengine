#pragma once

#include "components/camera.hpp"
#include <glad.h>
#include <algorithm>
#include <array>
#include <vector>

namespace editor::ui {
inline std::vector<unsigned char> read_camera_frame(const commons::camera& camera, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);

    GLint previous_texture{}, pack_buffer{};
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
    constexpr std::array<GLenum, 4> pack_parameters{
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_ROWS, GL_PACK_SKIP_PIXELS};
    std::array<GLint, pack_parameters.size()> previous_pack{};
    for (std::size_t i = 0; i < pack_parameters.size(); ++i) {
        glGetIntegerv(pack_parameters[i], &previous_pack[i]);
        glPixelStorei(pack_parameters[i], i == 0 ? 1 : 0);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, camera.framebuffer_texture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glBindTexture(GL_TEXTURE_2D, previous_texture);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
    for (std::size_t i = 0; i < pack_parameters.size(); ++i)
        glPixelStorei(pack_parameters[i], previous_pack[i]);

    // BGUI images store their first scanline at the top.
    const auto stride = static_cast<std::size_t>(width) * 3;
    for (int row = 0; row < height / 2; ++row) {
        const auto top = pixels.begin() + row * stride;
        std::swap_ranges(top, top + stride, pixels.begin() + (height - 1 - row) * stride);
    }
    return pixels;
}
}
