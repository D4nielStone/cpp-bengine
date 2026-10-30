#pragma once
#include "../systems/asset_import.hpp"
#include "../systems/project_file_transaction.hpp"
#include "loaders/model_import.hpp"
#include <bgui.hpp>
#include <elem/modal.hpp>
#include <elem/checkbox.hpp>
#include <elem/inputbox.hpp>
#include <elem/text.hpp>
#include <functional>
#include <memory>
#include <cmath>

namespace editor::ui {
class model_import_dialog {
    bgui::modal* m_modal = nullptr;
    std::shared_ptr<Assimp::Importer> m_importer;
    commons::model_import_options m_options;
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
        content.style.layout.require_width(bgui::mode::pixel, std::max(160, std::min(720, bgui::get_context().m_size.x - 100)));
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
        const auto check = [&](const std::string& label, bool& value) {
            modal.add<bgui::checkbox>(&value, value, label, .28f);
        };
        modal.add<bgui::text>("Configurações de malha", .32f);
        check("Inverter UVs", m_options.flip_uvs);
        check("Gerar normais suaves quando ausentes", m_options.smooth_normals);
        check("Unir vértices idênticos", m_options.join_vertices);
        modal.add<bgui::text>("Escala uniforme (> 0)", .28f);
        auto& scale = modal.add<bgui::inputbox>(std::to_string(m_options.scale), "Escala uniforme (> 0)", .28f);
        modal.add<bgui::text>("Nodes / estrutura da cena (desmarcar exclui a geometria dos descendentes)", .30f);
        const auto node_ui = [&](const auto& self, const aiNode* node, const std::string& path, unsigned depth) -> void {
            auto& toggle = modal.add<bgui::checkbox>(std::string(depth * 2, ' ') + node->mName.C_Str() +
                " [" + std::to_string(node->mNumMeshes) + " meshes]", .27f, !m_options.excluded_nodes.contains(path));
            toggle.set_on_change([this, path](bool enabled) {
                if (enabled) m_options.excluded_nodes.erase(path); else m_options.excluded_nodes.insert(path);
            });
            for (unsigned i = 0; i < node->mNumChildren; ++i) self(self, node->mChildren[i], path + "/" + std::to_string(i), depth + 1);
        };
        node_ui(node_ui, scene->mRootNode, "0", 0);
        modal.add<bgui::text>("Meshes / skins / skeleton", .32f);
        check("Importar skeleton e canais dos ossos", m_options.skeleton);
        check("Importar skins e pesos dos ossos (requer skeleton)", m_options.skins);
        for (unsigned i = 0; i < scene->mNumMeshes; ++i) {
            const auto* mesh = scene->mMeshes[i];
            auto& toggle = modal.add<bgui::checkbox>(std::string(mesh->mName.C_Str()) + " #" + std::to_string(i) +
                " — " + std::to_string(mesh->mNumVertices) + " vértices; " + std::to_string(mesh->mNumBones) + " bones",
                .27f, !m_options.excluded_meshes.contains(i));
            toggle.set_on_change([this, i](bool enabled) {
                if (enabled) m_options.excluded_meshes.erase(i); else m_options.excluded_meshes.insert(i);
            });
            for (unsigned b = 0; b < mesh->mNumBones; ++b)
                modal.add<bgui::text>("    Bone: " + std::string(mesh->mBones[b]->mName.C_Str()) + " — " +
                    std::to_string(mesh->mBones[b]->mNumWeights) + " pesos", .25f);
        }
        modal.add<bgui::text>("Animations", .32f);
        check("Importar e reproduzir animações (primeiro clipe selecionado)", m_options.animations);
        for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
            const auto* clip = scene->mAnimations[i];
            const double rate = clip->mTicksPerSecond > 0 ? clip->mTicksPerSecond : 25;
            auto& toggle = modal.add<bgui::checkbox>(std::string(clip->mName.C_Str()) + " #" + std::to_string(i) +
                " — " + std::to_string(clip->mDuration / rate) + " s; " + std::to_string(clip->mNumChannels) + " canais",
                .27f, !m_options.excluded_animations.contains(i));
            toggle.set_on_change([this, i](bool enabled) {
                if (enabled) m_options.excluded_animations.erase(i); else m_options.excluded_animations.insert(i);
            });
        }
        modal.add<bgui::text>("Materiais: " + std::to_string(scene->mNumMaterials) +
            "; texturas embutidas: " + std::to_string(scene->mNumTextures) +
            ". Câmeras, luzes e morph targets não são convertidos em entidades.", .27f);
        modal.add<bgui::button>("Importar / Reimportar", .30f,
            [this, source, project, destination, complete, scale = &scale, status = &status]() {
            try {
                if (!std::filesystem::is_regular_file(source)) throw std::runtime_error("Arquivo de origem ausente.");
                size_t consumed = 0;
                m_options.scale = std::stod(scale->get_buffer(), &consumed);
                if (consumed != scale->get_buffer().size() || !std::isfinite(m_options.scale) ||
                    m_options.scale <= 0 || m_options.scale > 1000000) throw std::runtime_error("Escala inválida.");
                Assimp::Importer validation;
                const auto* checked = validation.ReadFile(source.string(), m_options.flags());
                if (!checked || !checked->mRootNode) throw std::runtime_error(validation.GetErrorString());
                commons::validate_model_selection(*checked, m_options);
                (void)asset_dependencies(std::filesystem::canonical(source), true);
                const auto imported = import_asset(source, project, true, destination);
                const auto path = commons::model_import_settings_path(imported);
                project_files::stage({path, commons::serialize_model_import_options(m_options)});
                project_files::replace(path);
                status->set_buffer("Importação concluída: " + imported.filename().string());
                // Finish outside the click dispatch so callbacks can safely rebuild UI.
                bgui::add_function([this, imported, complete]() { close(); if (complete) complete(imported); });
            } catch (const std::exception& e) { status->set_buffer(e.what()); }
        });
    }
};
}
