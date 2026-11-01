#include "util/skybox.hpp"
#include <os/asset_manager.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>

using namespace COMMONS_NS;

skybox::~skybox() {
    if (glad_glGetString && glGetString(GL_VERSION)) {
        if (m_owned && id_skybox) glDeleteTextures(1, &id_skybox);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
    }
}

void skybox::draw(glm::mat4 view, glm::mat4 proj) {
    draw_background(view, proj, {}, {}, true, {0.43f, 0.78f, 0.86f}, {0.08f, 0.25f, 0.55f});
}

void skybox::draw_background(glm::mat4 view, glm::mat4 proj,
    const std::array<std::string, 6>& faces, const std::string& root,
    bool enabled, const bgui::vec3& horizon, const bgui::vec3& zenith) {
    draw_background(view, proj, faces, root, enabled, horizon, zenith, {});
}

void skybox::draw_background(glm::mat4 view, glm::mat4 proj,
    const std::array<std::string, 6>& faces, const std::string& root,
    bool enabled, const bgui::vec3& horizon, const bgui::vec3& zenith,
    const std::string& cross_image) {
    GLint vao, program, active, binding, unpack, row_length, skip_rows, skip_pixels;
    GLboolean depth_mask;
    const bool depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &binding);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
    glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skip_rows);
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skip_pixels);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    if (!enabled && !m_valid) m_attempted = false;
    if (enabled && (!m_attempted || m_faces != faces || m_root != root || m_image != cross_image)) {
        m_attempted = true; m_faces = faces; m_root = root; m_image = cross_image; m_valid = false;
        unsigned int candidate = 0;
        try {
            std::array<const bgui::texture*, 6> images{};
            int size = 0;
            const auto load = [&](const std::string& name) -> const bgui::texture& {
                if (name.empty()) throw std::runtime_error("Missing cubemap image");
                const std::filesystem::path path(name);
                return bgui::asset_manager::get_instance().load_texture(
                    (path.is_absolute() ? path : std::filesystem::path(root) / path).string());
            };
            if (!cross_image.empty()) {
                const auto& image = load(cross_image);
                const auto width = static_cast<int>(image.m_size.x);
                const auto height = static_cast<int>(image.m_size.y);
                if (width <= 0 || height <= 0 || width % 4 || height % 3 || width / 4 != height / 3 ||
                    image.m_buffer.size() != static_cast<size_t>(width) * height * (image.m_has_alpha ? 4 : 3))
                    throw std::runtime_error("Cubemap image must be a horizontal 4 x 3 cross with square faces");
                size = width / 4;
                images.fill(&image);
            } else {
                for (unsigned i = 0; i < 6; ++i) {
                    images[i] = &load(faces[i]);
                    const auto& image = *images[i];
                    const auto width = static_cast<int>(image.m_size.x);
                    if (width <= 0 || image.m_size.y != width || (size && size != width) ||
                        image.m_buffer.size() != static_cast<size_t>(width) * width * (image.m_has_alpha ? 4 : 3))
                        throw std::runtime_error("Cubemap faces must be equally sized square RGB/RGBA images");
                    size = width;
                }
            }
            GLint maximum; glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &maximum);
            if (size > maximum) throw std::runtime_error("Cubemap exceeds GPU limit");
            glGenTextures(1, &candidate);
            glBindTexture(GL_TEXTURE_CUBE_MAP, candidate);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            const int tiles[6][2] = {{2,1}, {0,1}, {1,0}, {1,2}, {1,1}, {3,1}};
            for (unsigned i = 0; i < 6; ++i) {
                const auto& image = *images[i];
                glPixelStorei(GL_UNPACK_ROW_LENGTH, cross_image.empty() ? 0 : static_cast<int>(image.m_size.x));
                glPixelStorei(GL_UNPACK_SKIP_PIXELS, cross_image.empty() ? 0 : tiles[i][0] * size);
                glPixelStorei(GL_UNPACK_SKIP_ROWS, cross_image.empty() ? 0 : tiles[i][1] * size);
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB8,
                    size, size, 0, image.m_has_alpha ? GL_RGBA : GL_RGB,
                    GL_UNSIGNED_BYTE, image.m_buffer.data());
            }
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            for (auto axis : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
                glTexParameteri(GL_TEXTURE_CUBE_MAP, axis, GL_CLAMP_TO_EDGE);
            if (m_owned && id_skybox) glDeleteTextures(1, &id_skybox);
            id_skybox = candidate; m_owned = true; m_valid = true;
        } catch (const std::exception&) {
            if (candidate) glDeleteTextures(1, &candidate);
        }
    }
    if (!m_vao) glGenVertexArrays(1, &m_vao);
    shader background("background.vert", "background.frag");
    background.use();
    const auto inverse = glm::inverse(glm::mat4(glm::mat3(view))) * glm::inverse(proj);
    background.setMat4("inverseCamera", glm::value_ptr(inverse));
    background.setVec3("horizon", horizon);
    background.setVec3("zenith", zenith);
    background.setBool("useCubemap", enabled && id_skybox && (!m_owned || m_valid));
    background.setInt("sky", 0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id_skybox);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(vao); glUseProgram(program); glDepthMask(depth_mask);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpack);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, skip_rows);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, skip_pixels);
    glBindTexture(GL_TEXTURE_CUBE_MAP, binding); glActiveTexture(active);
}
