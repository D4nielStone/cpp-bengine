#include <glad.h>
#include "commons_namespace.hpp"
#include "util/texture.hpp"
#include <os/asset_manager.hpp>
#include <os/resource_files.hpp>
#include <bgui_backend_gl3.hpp>
#include <stdexcept>

void commons::texture::bind(int slot) const {
    unsigned int texture_id = id;
    if (!texture_id && embedded) texture_id = bgui::gl3_get_texture(*embedded);
    if (!texture_id && !path.empty()) {
        auto& assets = bgui::asset_manager::get_instance();
        const auto& resolved_path = assets.resolve_asset_path(path);
        if (!bgui::resource_files::exists(resolved_path))
            throw std::runtime_error("[Texture] Asset source is missing: " + path);
        const auto& decoded = assets.load_texture(path);
        texture_id = bgui::gl3_get_texture(decoded);
    }
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, texture_id);
}
