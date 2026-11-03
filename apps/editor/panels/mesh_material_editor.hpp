#pragma once

#include "panel.hpp"
#include "editor_context.hpp"
#include "components/renderer.hpp"
#include "util/pbr_material.hpp"
#include "loaders/material_asset.hpp"
#include <elem/image.hpp>
#include <os/asset_manager.hpp>

namespace editor::ui {

struct mesh_material_binding {
    std::weak_ptr<editor_context> context;
    std::weak_ptr<commons::renderer> renderer;
    commons::model* model{};
    std::size_t index{};

    std::shared_ptr<commons::renderer> current() const {
        const auto owner = context.lock();
        const auto value = renderer.lock();
        if (!owner || !owner->registry || !value || value->m_modelo != model || !model ||
            !owner->registry->is_alive(value->my_object) ||
            owner->registry->get<commons::renderer>(value->my_object) != value || index >= model->meshes.size()) return {};
        return value;
    }
    commons::material read() const {
        const auto value = current();
        return value ? value->m_modelo->meshes[index].getMaterial() : commons::material{};
    }
    void write(const commons::material& material) const {
        if (const auto value = current()) value->m_modelo->meshes[index].setMaterial(material);
    }
    void commit(const commons::material& next) const {
        const auto owner = context.lock();
        if (!owner || owner->is_playing || !current()) return;
        const auto before = read();
        write(next);
        owner->record_action([binding = *this, before] { binding.write(before); },
            [binding = *this, next] { binding.write(next); });
    }
};

using material_drop_target = std::function<void(bgui::element*, std::function<void(const std::filesystem::path&)>)>;

inline void build_mesh_material_editor(panel& owner, bgui::linear& content,
    const std::shared_ptr<editor_context>& context, const std::shared_ptr<commons::renderer>& renderer,
    std::size_t index, const material_drop_target& register_target,
    const std::function<void(std::string)>& status) {
    const mesh_material_binding binding{context, renderer, renderer->m_modelo, index};
    const auto material = binding.read();
    content.add<bgui::text>("Material .bmat", .27f).receives_input(false);
    auto& material_row = content.add<bgui::linear>(bgui::orientation::horizontal);
    panel::configure_property_row(material_row);
    auto& material_input = panel::add_input(material_row, "", "Carregar .bmat", .26f);
    material_input.add_class("material-asset-path");
    panel::configure_property_input(material_input);
    const auto load = [binding, context, status](const std::filesystem::path& requested) {
        if (!binding.current() || context->is_playing || requested.empty()) return;
        const auto path = requested.is_absolute() ? requested : context->project_root / requested;
        commons::material next;
        std::string error;
        if (!commons::load_material_asset(path, next, error)) { status(error); return; }
        binding.commit(next);
        status("Material carregado: " + path.filename().string());
    };
    material_input.m_enter_func = [load](const std::string& path) { load(path); };
    if (register_target) register_target(&material_input, load);
    material_row.add<bgui::button>("Salvar .bmat", .26f, [binding, context, status] {
        if (!binding.current() || context->is_playing) return;
        std::filesystem::path path;
        std::string error;
        if (!commons::store_material_asset(context->project_root / "assets/materials", binding.read(), path, error))
            status(error);
        else status("Material salvo: " + path.string());
    });
    const auto albedo = material.getUniforme("material.albedo");
    commons::color color;
    if (albedo.m_type == commons::uniform::type::Cor) color = albedo.valor.color_value;
    else if (albedo.m_type == commons::uniform::type::Cor_ptr && albedo.ponteiro.color_ptr) color = *albedo.ponteiro.color_ptr;
    content.add<bgui::text>("Base Color", .27f).receives_input(false);
    auto& picker = panel::add_color(content, {color.r, color.g, color.b, color.a});
    picker.set_on_change([binding](const bgui::color& value) {
        auto next = binding.read();
        const auto previous = next.getUniforme("material.albedo");
        if (previous.m_type == commons::uniform::type::Cor) {
            const auto& c = previous.valor.color_value;
            if (c.r == value.x && c.g == value.y && c.b == value.z && c.a == value.w) return;
        }
        next.set_uniform("material.albedo", commons::color{value.x,value.y,value.z,value.w});
        binding.commit(next);
    });
    const auto add_factor = [&](const commons::pbr_factor& factor, bool unit_range) {
        owner.add_float(content, factor.label, commons::pbr_value(material, factor),
            [binding, factor] { return commons::pbr_value(binding.read(), factor); },
            [binding, factor, unit_range](float value) {
                if (!binding.current()) return;
                auto next = binding.read();
                next.set_uniform(std::string("material.") + factor.name, unit_range ? std::clamp(value,0.f,1.f) : value);
                binding.write(next);
            });
    };
    for (const auto& factor : commons::pbr_factors) add_factor(factor, true);
    auto& world = panel::add_checkbox(content, "WorldTexture", .29f, commons::world_texture(material));
    world.set_on_change([binding](bool enabled) {
        auto next = binding.read();
        if (commons::world_texture(next) == enabled) return;
        next.set_uniform("uvMundo", enabled);
        binding.commit(next);
    });
    for (const auto& factor : commons::texture_factors) add_factor(factor, false);
    for (const auto& map : commons::pbr_maps) {
        content.add<bgui::text>(map.label, .27f).receives_input(false);
        auto& row = content.add<bgui::linear>(bgui::orientation::horizontal);
        panel::configure_property_row(row);
        auto& input = panel::add_input(row, material.getTextura(map.name).path, "Caminho da textura", .26f);
        input.add_class("material-texture-path");
        panel::configure_property_input(input);
        auto& preview = content.add<bgui::image>();
        preview.add_class("material-texture-preview");
        preview.style.layout.require_mode(bgui::mode::pixel,bgui::mode::pixel);
        preview.style.layout.require_size(96.f,72.f);
        const auto show_preview = [context, preview = &preview](const std::string& value) {
            preview->set_enable(false);
            if (value.empty()) return;
            try {
                const auto path = std::filesystem::path(value);
                preview->set_texture(bgui::asset_manager::get_instance().load_texture(
                    (path.is_absolute() ? path : context->project_root / path).string()));
                preview->set_enable(true);
            } catch (const std::exception&) {}
        };
        show_preview(material.getTextura(map.name).path);
        const auto assign = [binding, map, show_preview, context, status, input = &input](const std::filesystem::path& requested) {
            if (!binding.current() || context->is_playing) return;
            auto next = binding.read();
            if (requested.empty()) {
                next.textures.erase(map.name);
                next.set_uniform(std::string("use_") + map.name, false);
            } else try {
                const auto path = requested.is_absolute() ? requested : context->project_root / requested;
                const auto& decoded = bgui::asset_manager::get_instance().load_texture(path.string());
                next.set_texture(map.name, commons::texture{0, decoded.m_path});
                next.set_uniform(std::string("use_") + map.name, true);
            } catch (const std::exception& error) { status(error.what()); return; }
            binding.commit(next);
            input->set_buffer(next.getTextura(map.name).path);
            show_preview(next.getTextura(map.name).path);
            status("");
        };
        input.m_enter_func = [assign](const std::string& path) { assign(path); };
        if (register_target) register_target(&input, assign);
        auto& clear = row.add<bgui::button>("×", .26f, [assign] { assign({}); });
        clear.style.layout.require_mode(bgui::mode::pixel,bgui::mode::pixel);
        clear.style.layout.require_size(22.f,22.f);
    }
}
}
