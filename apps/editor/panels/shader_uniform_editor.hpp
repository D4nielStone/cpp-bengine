#pragma once
#include "loaders/material_asset.hpp"
#include "util/material.hpp"
#include "util/pbr_material.hpp"
#include <elem/inputbox.hpp>
#include <elem/text.hpp>
#include <lay/linear.hpp>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <functional>
#include <string_view>

namespace editor::ui {
inline bool engine_shader_uniform(std::string_view name) {
    for (const auto prefix : {"dirLight.", "pointLights[", "spotLights[", "transformacoes[", "use_tex_"})
        if (name.starts_with(prefix)) return true;
    for (const auto reserved : {"model", "view", "projection", "viewPos", "instance", "instance_id", "uvMundo", "shadowEnabled", "shadowMap",
        "spotShadowMap", "lightSpaceMatrix", "shadowBias", "shadowStrength", "pointLightCount", "spotLightCount",
        "fogEnabled", "fogColor", "fogDensity", "fogMinDistance", "fogMaxDistance", "sceneColor", "viewportSize", "engineTime"})
        if (name == reserved) return true;
    if (name == "material.albedo") return true;
    for (const auto& factor : commons::pbr_factors)
        if (name == std::string("material.") + factor.name) return true;
    for (const auto& factor : commons::texture_factors)
        if (name == std::string("material.") + factor.name) return true;
    return false;
}

inline void build_shader_uniform_editor(bgui::linear& content, const std::string& vertex, const std::string& fragment,
    const std::function<commons::material()>& read, const std::function<bool(const commons::material&)>& commit,
    const std::function<void(std::string)>& status) {
    if (fragment.empty() || !glad_glGetString || !glGetString(GL_VERSION)) return;
    commons::shader program(vertex.c_str(), fragment.c_str());
    if (!program.ID) { status("Não foi possível compilar o shader GLSL."); return; }
    const auto parameters = read();
    rapidjson::Document encoded;
    rapidjson::Value uniforms;
    try { uniforms = commons::serialize_uniforms(parameters, encoded.GetAllocator()); }
    catch (const std::exception& error) { status(error.what()); return; }
    for (const auto& info : program.active_uniforms()) {
        if (engine_shader_uniform(info.name)) continue;
        const int tag = info.type == "float" ? 0 : info.type == "int" ? 1 : info.type == "bool" ? 2 :
            info.type == "vec2" ? 3 : info.type == "vec3" ? 4 : info.type == "vec4" ? 5 : info.type == "mat4" ? 7 : -1;
        for (int i = 0; i < info.size; ++i) {
            auto name = info.name;
            if (const auto offset = name.find("[0]"); offset != std::string::npos)
                name.replace(offset, 3, "[" + std::to_string(i) + "]");
            content.add<bgui::text>(name + " (" + info.type + ")", .30f).receives_input(false);
            if (tag < 0) continue;
            std::string text;
            if (uniforms.HasMember(name.c_str())) {
                rapidjson::StringBuffer buffer;
                rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
                uniforms[name.c_str()]["value"].Accept(writer);
                text = buffer.GetString();
                if (text.starts_with('[')) text = text.substr(1, text.size() - 2);
            }
            auto& input = content.add<bgui::inputbox>(text, tag == 2 ? "true / false; vazio = padrão GLSL" : "Valores separados por vírgula; vazio = padrão GLSL", .30f,
                [read, commit, status, name, tag](const std::string& text) {
                    auto next = read();
                    if (text.empty()) { next.uniforms.erase(name); commit(next); return; }
                    rapidjson::Document entries(rapidjson::kObjectType), data;
                    const auto source = (tag == 1 || tag == 2) ? text : "[" + text + "]";
                    data.Parse(source.data(), source.size());
                    if (data.HasParseError()) { status("Valor de uniform inválido."); return; }
                    auto& allocator = entries.GetAllocator();
                    rapidjson::Value entry(rapidjson::kObjectType);
                    entry.AddMember("type", tag, allocator);
                    entry.AddMember("value", rapidjson::Value(data, allocator), allocator);
                    entries.AddMember(rapidjson::Value(name.c_str(), allocator), entry, allocator);
                    commons::material parsed;
                    if (!commons::analyze_uniforms(entries, parsed)) { status("Tipo ou quantidade de valores do uniform inválidos."); return; }
                    next.set_uniform(name, parsed.getUniforme(name));
                    if (commit(next)) status("");
                });
            input.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }
    }
}
}
