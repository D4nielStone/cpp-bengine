#pragma once
#include "panel.hpp"
#include "../systems/editable_model_import.hpp"
#include "loaders/model_import.hpp"
#include <bgui.hpp>
#include <elem/modal.hpp>
#include <elem/checkbox.hpp>
#include <elem/inputbox.hpp>
#include <elem/text.hpp>
#include <elem/image.hpp>
#include "model_import_preview.hpp"
#include <functional>
#include <memory>
#include <cmath>
#include <charconv>

namespace editor::ui {
class model_import_dialog {
    bgui::modal* m_modal = nullptr;
    std::shared_ptr<Assimp::Importer> m_importer;
    commons::model_import_options m_options;
    bool m_another_copy{false};
public:
    ~model_import_dialog() { close(); }
    void close() {
        if (m_modal) bgui::get_layout().remove(m_modal);
        m_modal = nullptr;
        m_importer.reset();
    }
    bool open() const { return m_modal != nullptr; }
    void show(std::filesystem::path source, std::filesystem::path project,
        std::filesystem::path destination = {}, std::string diagnostic = {},
        std::function<void(const std::filesystem::path&)> complete = {}) {
        close();
        m_options = {};
        m_another_copy = false;
        try { m_options = commons::read_model_import_options(source); }
        catch (const std::exception& e) { diagnostic += std::string("\n") + e.what(); }
        m_importer = std::make_shared<Assimp::Importer>();
        const auto* scene = m_importer->ReadFile(source.string(), m_options.flags());
        auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
        m_modal = &modal;
        modal.set_confirmation_text("Fechar / Cancelar");
        modal.set_on_confirm([this]() { m_modal = nullptr; m_importer.reset(); });
        auto& content = modal.content();
        content.style.layout.require_height(bgui::mode::pixel, std::max(100, std::min(520, bgui::get_context().m_size.y - 150)));
        const int dialog_width = std::max(160, std::min(1000, bgui::get_context().m_size.x - 100));
        content.style.layout.require_width(bgui::mode::pixel, dialog_width);
        content.set_scrollable(true);
        modal.add<bgui::text>("Importador 3D — " + source.filename().string(), .36f);
        modal.add<bgui::text>(source.string(), .25f);
        auto& status = modal.add<bgui::text>(diagnostic, .27f);
        if (!scene || !scene->mRootNode) {
            status.set_buffer(diagnostic + "\nAssimp: " + m_importer->GetErrorString());
            modal.add<bgui::text>("Corrija o arquivo ou suas dependências e tente novamente. O original foi preservado.", .27f);
            modal.add<bgui::button>("Tentar novamente", .28f, [this, source, project, destination, complete]() {
                bgui::add_function([this, source, project, destination, complete]() { show(source, project, destination, {}, complete); });
            });
            return;
        }
        auto& columns = modal.add<bgui::linear>(bgui::orientation::horizontal);
        columns.style.layout.require_mode(bgui::mode::stretch, bgui::mode::stretch);
        auto& settings = columns.add<bgui::linear>(bgui::orientation::vertical);
        settings.style.layout.require_mode(bgui::mode::pixel, bgui::mode::stretch);
        const float settings_width = std::clamp(dialog_width * .42f, 160.f, 400.f);
        const float preview_width = std::min(480.f,
            std::max(160.f, dialog_width - settings_width - 48.f));
        settings.style.layout.require_width(bgui::mode::pixel, settings_width);
        settings.set_scrollable(true);
        auto& preview_panel = columns.add<bgui::linear>(bgui::orientation::vertical);
        preview_panel.style.layout.require_mode(bgui::mode::stretch, bgui::mode::stretch);
        preview_panel.add<bgui::text>("Visualização 3D orbital", .32f);
        auto& preview = preview_panel.add<bgui::image>(model_import_preview(*scene, m_options));
        preview.set_size(preview_width, preview_width * 260.f / 480.f);
        auto& orbit = preview_panel.add<model_import_orbit>(m_importer, *scene, m_options, preview);
        const auto refresh_preview = [&orbit]() { orbit.refresh(); };
        preview_panel.add<bgui::text>("Arraste para orbitar · use a roda do mouse para aproximar ou afastar.", .27f);
        preview_panel.add<bgui::button>("Redefinir órbita", .28f, [&orbit]() { orbit.reset(); });
        preview_panel.add<bgui::button>("Atualizar prévia da seleção", .28f, refresh_preview);
        const auto check = [&](const std::string& label, bool& value) {
            panel::add_checkbox(settings, &value, value, label, .28f)
                .set_on_change([refresh_preview](bool) { refresh_preview(); });
        };
        check("Importar outra cópia", m_another_copy);
        settings.add<bgui::text>("Configurações de malha", .32f);
        settings.add<bgui::text>("Filtragem de texturas (suavização)", .28f);
        auto& filter_label = settings.add<bgui::text>("", .28f);
        const auto refresh_filter = [this, label = &filter_label]() {
            label->set_buffer(m_options.filtering == commons::texture_filter::bilinear ? "Bilinear" :
                m_options.filtering == commons::texture_filter::trilinear ? "Trilinear" : "Original");
        };
        refresh_filter();
        for (const auto& [label, value] : {std::pair{"Original", commons::texture_filter::original},
            {"Bilinear", commons::texture_filter::bilinear}, {"Trilinear", commons::texture_filter::trilinear}})
            settings.add<bgui::button>(label, .28f, [this, value, refresh_filter]() {
                m_options.filtering = value;
                refresh_filter();
            });
        check("Inverter UVs", m_options.flip_uvs);
        check("Gerar normais suaves quando ausentes", m_options.smooth_normals);
        check("Unir vértices idênticos", m_options.join_vertices);
        settings.add<bgui::text>("Escala uniforme (> 0)", .28f);
        auto& scale = panel::add_input(settings, std::to_string(m_options.scale), "Escala uniforme (> 0)", .28f);
        scale.m_enter_func = [this, refresh_preview](const std::string& value) {
            double next_scale = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), next_scale);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() &&
                std::isfinite(next_scale) && next_scale > 0 && next_scale <= 1000000) {
                m_options.scale = next_scale;
                refresh_preview();
            }
        };
        settings.add<bgui::text>("Nodes / estrutura da cena (desmarcar exclui a geometria dos descendentes)", .30f);
        const auto node_ui = [&](const auto& self, const aiNode* node, const std::string& path, unsigned depth) -> void {
            auto& toggle = panel::add_checkbox(settings, std::string(depth * 2, ' ') + node->mName.C_Str() +
                " [" + std::to_string(node->mNumMeshes) + " meshes]", .27f, !m_options.excluded_nodes.contains(path));
            toggle.set_on_change([this, path, refresh_preview](bool enabled) {
                if (enabled) m_options.excluded_nodes.erase(path); else m_options.excluded_nodes.insert(path);
                refresh_preview();
            });
            for (unsigned i = 0; i < node->mNumChildren; ++i) self(self, node->mChildren[i], path + "/" + std::to_string(i), depth + 1);
        };
        node_ui(node_ui, scene->mRootNode, "0", 0);
        settings.add<bgui::text>("Meshes / skins / skeleton", .32f);
        check("Importar skeleton e canais dos ossos", m_options.skeleton);
        check("Importar skins e pesos dos ossos (requer skeleton)", m_options.skins);
        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            const auto* mesh = scene->mMeshes[i];
            auto& toggle = panel::add_checkbox(settings, std::string(mesh->mName.C_Str()) + " #" + std::to_string(i) +
                " — " + std::to_string(mesh->mNumVertices) + " vértices; " + std::to_string(mesh->mNumBones) + " bones",
                .27f, !m_options.excluded_meshes.contains(i));
            toggle.set_on_change([this, i, refresh_preview](bool enabled) {
                if (enabled) m_options.excluded_meshes.erase(i); else m_options.excluded_meshes.insert(i);
                refresh_preview();
            });
            for (unsigned b = 0; b < mesh->mNumBones; ++b)
                settings.add<bgui::text>("    Bone: " + std::string(mesh->mBones[b]->mName.C_Str()) + " — " +
                    std::to_string(mesh->mBones[b]->mNumWeights) + " pesos", .25f);
        }
        settings.add<bgui::text>("Animations", .32f);
        check("Importar e reproduzir animações (primeiro clipe selecionado)", m_options.animations);
        for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
            const auto* clip = scene->mAnimations[i];
            const double rate = clip->mTicksPerSecond > 0 ? clip->mTicksPerSecond : 25;
            auto& toggle = panel::add_checkbox(settings, std::string(clip->mName.C_Str()) + " #" + std::to_string(i) +
                " — " + std::to_string(clip->mDuration / rate) + " s; " + std::to_string(clip->mNumChannels) + " canais",
                .27f, !m_options.excluded_animations.contains(i));
            toggle.set_on_change([this, i, refresh_preview](bool enabled) {
                if (enabled) m_options.excluded_animations.erase(i); else m_options.excluded_animations.insert(i);
                refresh_preview();
            });
        }
        settings.add<bgui::text>("Materiais: " + std::to_string(scene->mNumMaterials) +
            "; texturas embutidas: " + std::to_string(scene->mNumTextures) +
            ". Câmeras, luzes e morph targets não são convertidos em entidades.", .27f);
        modal.add<bgui::button>("Importar como .bmodel", .30f,
            [this, source, project, destination, complete, scale = &scale, status = &status]() {
            try {
                if (!std::filesystem::is_regular_file(source)) throw std::runtime_error("Arquivo de origem ausente.");
                size_t consumed = 0;
                m_options.scale = std::stod(scale->get_buffer(), &consumed);
                if (consumed != scale->get_buffer().size() || !std::isfinite(m_options.scale) ||
                    m_options.scale <= 0 || m_options.scale > 1000000) throw std::runtime_error("Escala inválida.");
                const auto imported = import_native_model(source, project, destination, m_another_copy, &m_options);
                status->set_buffer("Importação concluída: " + imported.filename().string());
                // Finish outside the click dispatch so callbacks can safely rebuild UI.
                bgui::add_function([this, imported, complete]() { close(); if (complete) complete(imported); });
            } catch (const std::exception& e) { status->set_buffer(e.what()); }
        });
    }
};
}
