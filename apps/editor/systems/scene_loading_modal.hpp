#pragma once
#include "project_persistence_system.hpp"
#include "resources/scene_asset_loader.hpp"
#include <bgui.hpp>
#include <bgui_backend_glfw.hpp>
#include <bgui_backend_gl3.hpp>
#include <glad.h>
#include <chrono>
#include "loaders/material_asset.hpp"

namespace editor::ui {
// Call between UI frames, on the GL context owner, with gameplay systems paused.
inline bool run_project_with_modal(project_persistence_system& project,
    const std::string& path, std::string& error, bool saving) {
    commons::scene_asset_loader assets;
    bool cancelled = false;
    auto* modal = &bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
    modal->set_confirmation_text("Cancelar");
    modal->set_on_confirm([&] { cancelled = true; modal = nullptr; });
    modal->add<bgui::text>((saving ? "Preparando Play — " : "Carregando cena — ") + std::filesystem::path(path).filename().string(), .36f);
    auto& status = modal->add<bgui::text>("Preparando recursos", .27f);
    auto& bar = modal->add<bgui::progress_bar>(0.f, 100.f, 0.f);
    bar.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto next_frame = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    const auto pump = [&](const commons::scene_asset_loader::progress& progress) {
        if (cancelled || bgui::should_close_glfw()) return false;
        const auto now = std::chrono::steady_clock::now();
        if (now < next_frame || progress.state == commons::scene_asset_loader::phase::ready)
            return true;
        next_frame = now + std::chrono::milliseconds(16);
        if (!cancelled) {
            bar.set_value(progress.fraction * 100.f);
            using phase = commons::scene_asset_loader::phase;
            const char* step = progress.state == phase::importing ? "Importando" :
                progress.state == phase::decoding ? "Decodificando" :
                progress.state == phase::uploading ? "Enviando à GPU" : "Pronto";
            status.set_buffer(std::to_string(int(progress.fraction * 100.f)) + "% — " + step + " — " + progress.current);
        }
        // Render only UI: never run ECS systems against an incomplete scene.
        bgui::glfw_update(bgui::get_context());
        bgui::load_font_queue();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        bgui::on_update();
        const auto srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glDisable(GL_FRAMEBUFFER_SRGB);
        bgui::gl3_clear();
        bgui::gl3_render(bgui::get_draw_data());
        if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
        bgui::swap_glfw();
        return !cancelled && !bgui::should_close_glfw();
    };
    project.set_asset_loading_hooks({
        [&](const auto& paths) { assets.prepare(paths, pump); },
        [&](const auto& asset) { return assets.instantiate(asset); },
        [&](const auto& staging) { assets.finish(staging, pump); },
        [&](const auto& path) { return assets.load_material(path, pump); }
    });
    if (saving) project.set_material_save_hook([&](const commons::material& source, const std::filesystem::path& directory) {
        commons::scene_asset_loader::progress progress;
        progress.state = commons::scene_asset_loader::phase::importing;
        progress.current = "Salvando materiais";
        if (!pump(progress)) throw std::runtime_error("Scene saving cancelled.");
        auto job = std::async(std::launch::async, [source, directory] {
            std::filesystem::path result;
            std::string error;
            if (!commons::store_material_asset(directory, source, result, error)) throw std::runtime_error(error);
            return result;
        });
        bool cancelled = false;
        while (job.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            if (!pump(progress)) cancelled = true;
        auto result = job.get();
        if (cancelled || !pump(progress)) throw std::runtime_error("Scene saving cancelled.");
        return result;
    });
    struct cleanup {
        project_persistence_system& project;
        bgui::modal*& modal;
        ~cleanup() {
            project.set_asset_loading_hooks({});
            project.set_material_save_hook({});
            if (modal) bgui::get_layout().remove(modal);
        }
    } cleanup{project, modal};
    return saving ? project.save(path, error) : project.load(path, error);
}
inline bool load_project_with_modal(project_persistence_system& project,
    const std::string& path, std::string& error) {
    return run_project_with_modal(project, path, error, false);
}
inline bool save_project_with_modal(project_persistence_system& project,
    const std::string& path, std::string& error) {
    return run_project_with_modal(project, path, error, true);
}
}
