#include "commons_namespace.hpp"
#include "util/texture.hpp"
#include <os/asset_manager.hpp>
#include <bgui_backend_gl3.hpp>

void commons::texture::bind(int slot) const {
    unsigned int texture_id = id;
    if (!texture_id && !path.empty()) {
        const auto& decoded = bgui::asset_manager::get_instance().load_texture(path);
        texture_id = bgui::gl3_get_texture(decoded);
    }
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}
