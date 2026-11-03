#include "resources/scene_asset_loader.hpp"
#include "components/renderer.hpp"
#include "loaders/material_asset.hpp"
#include <os/asset_manager.hpp>
#include <os/resource_files.hpp>
#include <bgui_backend_gl3.hpp>
#include <glad.h>
#include <unordered_set>

namespace COMMONS_NS {
void scene_asset_loader::notify(const pump& update) {
    if (!update(m_status)) {
        m_status.state = phase::cancelled;
        throw std::runtime_error("Scene loading cancelled.");
    }
}
void scene_asset_loader::prepare(const std::vector<std::string>& paths, const pump& update) try {
    m_models.clear();
    m_materials.clear();
    m_status = {};
    m_status.state = phase::importing;
    std::unordered_set<std::string> unique(paths.begin(), paths.end());
    m_status.total = unique.size();
    for (const auto& path : unique) {
        m_status.current = path;
        notify(update);
        auto imported = wait(std::async(std::launch::async, [path] { return model::prepare(path); }), update);
        m_models.emplace(path, std::move(imported));
        ++m_status.imported;
        m_status.fraction = .45f * float(m_status.imported) / float(m_status.total);
        notify(update);
    }
    m_status.fraction = .45f;
} catch (const std::exception& error) {
    if (m_status.state != phase::cancelled) m_status.state = phase::failed;
    m_status.error = error.what();
    throw;
}
std::unique_ptr<model> scene_asset_loader::instantiate(const std::string& path) const {
    return m_models.at(path)->clone();
}
material scene_asset_loader::load_material(const std::filesystem::path& path, const pump& update) try {
    m_status.state = phase::decoding;
    const auto key = path.lexically_normal().string();
    m_status.current = key;
    notify(update);
    if (const auto found = m_materials.find(key); found != m_materials.end()) return found->second;
    auto loaded = wait(std::async(std::launch::async, [path] {
        material result;
        std::string error;
        if (!load_material_asset(path, result, error)) throw std::runtime_error(error);
        return result;
    }), update);
    return m_materials.emplace(key, std::move(loaded)).first->second;
} catch (const std::exception& error) {
    if (m_status.state != phase::cancelled) m_status.state = phase::failed;
    m_status.error = error.what();
    throw;
}
void scene_asset_loader::finish(const std::shared_ptr<ecs>& staging, const pump& update) try {
    m_status.state = phase::decoding;
    std::unordered_map<std::string, std::shared_ptr<bgui::texture>> textures;
    std::vector<std::pair<mesh*, std::string>> meshes;
    for (const auto& [id, components] : staging->entities) {
        auto renderer = staging->get<COMMONS_NS::renderer>(id);
        if (!renderer || !renderer->m_modelo) continue;
        for (auto& mesh : renderer->m_modelo->meshes) {
            meshes.emplace_back(&mesh, renderer->m_modelo->get_source_path());
            for (const auto& [name, texture] : mesh.getMaterial().textures)
                if (!texture.id && !texture.embedded && !texture.path.empty()) textures.emplace(texture.path, nullptr);
        }
    }
    std::size_t done = 0;
    for (auto& [path, decoded] : textures) {
        m_status.current = path;
        notify(update);
        const auto resolved = bgui::asset_manager::get_instance().resolve_asset_path(path);
        decoded = wait(std::async(std::launch::async, [resolved] {
            return std::make_shared<bgui::texture>(bgui::decode_texture_memory(bgui::resource_files::read(resolved), resolved));
        }), update);
        m_status.fraction = .45f + .2f * float(++done) / float(textures.size());
        notify(update);
    }
    m_status.fraction = .65f;
    m_status.state = phase::uploading;
    struct bindings {
        GLint vao{}, buffer{}, texture{}, active{};
        bindings() {
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &buffer);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        }
        ~bindings() {
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glActiveTexture(active);
            glBindTexture(GL_TEXTURE_2D, texture);
        }
    };
    std::unordered_map<const bgui::texture*, GLuint> uploaded_textures;
    const auto upload_texture = [&](const bgui::texture& decoded, const std::string& path) {
        const auto found = uploaded_textures.find(&decoded);
        if (found != uploaded_textures.end()) return found->second;
        const auto id = bgui::gl3_get_texture(decoded);
        if (!id) throw std::runtime_error("Texture upload failed: " + path);
        uploaded_textures.emplace(&decoded, id);
        return id;
    };
    done = 0;
    for (auto& [mesh, path] : meshes) {
        m_status.current = path;
        notify(update);
        {
            bindings restore;
            auto material = mesh->getMaterial();
            for (auto& [name, texture] : material.textures) {
                if (!texture.id && !texture.embedded && !texture.path.empty()) {
                    texture.id = upload_texture(*textures.at(texture.path), texture.path);
                }
                if (texture.embedded) {
                    m_status.current = texture.path;
                    upload_texture(*texture.embedded, texture.path);
                }
            }
            mesh->setMaterial(material);
            mesh->load();
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL scene upload failed.");
        }
        m_status.fraction = .65f + .35f * float(++done) / float(meshes.size());
        notify(update);
    }
    m_status.fraction = 1.f;
    m_status.current = "Ready";
    m_status.state = phase::ready;
    notify(update);
} catch (const std::exception& error) {
    if (m_status.state != phase::cancelled) m_status.state = phase::failed;
    m_status.error = error.what();
    throw;
}
}
