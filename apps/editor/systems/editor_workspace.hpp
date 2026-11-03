#pragma once

#include "ui_persistence_system.hpp"
#include "files_view.hpp"
#include "editor_context.hpp"
#include "components/camera.hpp"
#include "components/transform.hpp"
#include "project_file_transaction.hpp"

#include <filesystem>
#include <cmath>
#include <string>

namespace editor::ui {

class editor_workspace final {
public:
    explicit editor_workspace(ui_persistence_system& persistence, std::shared_ptr<editor_context> context = {})
        : m_context(std::move(context)), m_persistence(persistence) {}

    void set_project_file(const std::filesystem::path& project_file) {
        std::error_code error;
        auto absolute_path = std::filesystem::absolute(project_file, error);
        m_root = error ? project_file.parent_path() : absolute_path.parent_path();
        m_session_path = m_root / ".editor-session.cfg";
        m_layout_path = m_root / ".editor-ui.cfg";
    }

    bool load_workspace(files_view& files, std::string& error) const {
        error.clear();
        if (!load_workflow(error)) return false;
        files.restore_session(m_session_path);
        std::error_code filesystem_error;
        if (!std::filesystem::exists(m_layout_path, filesystem_error)) {
            if (filesystem_error) {
                error = "Could not inspect workspace layout '" +
                    m_layout_path.string() + "': " + filesystem_error.message();
                return false;
            }
            return true;
        }
        return m_persistence.load(m_layout_path.string(), error);
    }

    bool save_workspace(files_view& files, std::string& error) const {
        error.clear();
        if (!save_workflow(error)) return false;
        if (!files.save_session(m_session_path)) {
            error = "Could not save open documents to '" +
                m_session_path.string() + "'.";
            return false;
        }
        return m_persistence.save(m_layout_path.string(), error);
    }

private:
    std::shared_ptr<editor_context> m_context;
    bool save_workflow(std::string& error) const {
        if (!m_context) return true;
        rapidjson::Document doc(rapidjson::kObjectType);
        auto& a = doc.GetAllocator();
        doc.AddMember("version", 1, a);
        doc.AddMember("workspace", m_context->workspace_mode, a);
        doc.AddMember("model_view_mode", m_context->model_view_mode, a);
        doc.AddMember("model_selection", m_context->model_selection, a);
        doc.AddMember("extrude_distance", rapidjson::Value(m_context->extrude_distance.c_str(), a), a);
        doc.AddMember("model_orbital", m_context->model_orbital_view, a);
        doc.AddMember("orbital", m_context->orbital_view, a);
        doc.AddMember("colliders", m_context->show_colliders, a);
        if (m_context->selected_entity) doc.AddMember("selected", *m_context->selected_entity, a);
        rapidjson::Value sections(rapidjson::kObjectType);
        for (const auto& [name, expanded] : m_context->inspector_sections)
            sections.AddMember(rapidjson::Value(name.c_str(), a), rapidjson::Value(expanded), a);
        doc.AddMember("inspector", sections, a);
        if (m_context->editor_camera_entity) {
            auto id = *m_context->editor_camera_entity;
            if (auto camera = m_context->registry->get<commons::camera>(id)) {
                rapidjson::Value v(rapidjson::kObjectType); camera->serialize(v,a);
                doc.AddMember("camera", v, a);
            }
            if (auto transform = m_context->registry->get<commons::transform>(id)) {
                rapidjson::Value v(rapidjson::kObjectType); transform->serialize(v,a);
                doc.AddMember("transform", v, a);
            }
        }
        try {
            const auto path = m_root / ".editor-workflow.json";
            project_files::stage({path, project_files::encode(doc)});
            project_files::replace(path);
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    bool load_workflow(std::string& error) const {
        if (!m_context) return true;
        try {
            const auto path = m_root / ".editor-workflow.json";
            if (!std::filesystem::exists(path)) return true;
            const auto text = project_files::read(path);
            rapidjson::Document doc; doc.Parse(text.c_str());
            if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("version") ||
                !doc["version"].IsInt() || doc["version"].GetInt() != 1)
                throw std::runtime_error("Invalid editor workflow file: " + path.string());
            if (doc.HasMember("workspace") && doc["workspace"].IsInt() &&
                doc["workspace"].GetInt() >= 0 && doc["workspace"].GetInt() <= 2)
                m_context->workspace_mode = doc["workspace"].GetInt();
            if (doc.HasMember("model_view_mode") && doc["model_view_mode"].IsInt() && doc["model_view_mode"].GetInt()>=0 && doc["model_view_mode"].GetInt()<=2) m_context->model_view_mode=doc["model_view_mode"].GetInt();
            if (doc.HasMember("model_selection") && doc["model_selection"].IsInt() &&
                doc["model_selection"].GetInt() >= 0 && doc["model_selection"].GetInt() <= 3)
                m_context->model_selection = doc["model_selection"].GetInt();
            if (doc.HasMember("extrude_distance") && doc["extrude_distance"].IsString())
                m_context->extrude_distance = doc["extrude_distance"].GetString();
            if (doc.HasMember("model_orbital") && doc["model_orbital"].IsBool()) m_context->model_orbital_view = doc["model_orbital"].GetBool();
            if (doc.HasMember("orbital") && doc["orbital"].IsBool()) m_context->orbital_view = doc["orbital"].GetBool();
            if (doc.HasMember("colliders") && doc["colliders"].IsBool()) m_context->show_colliders = doc["colliders"].GetBool();
            m_context->selected_entity.reset();
            if (doc.HasMember("selected") && doc["selected"].IsUint() && m_context->registry->is_alive(doc["selected"].GetUint()))
                m_context->selected_entity = doc["selected"].GetUint();
            m_context->inspector_sections.clear();
            if (doc.HasMember("inspector") && doc["inspector"].IsObject())
                for (auto it = doc["inspector"].MemberBegin(); it != doc["inspector"].MemberEnd(); ++it)
                    if (it->value.IsBool()) m_context->inspector_sections[it->name.GetString()] = it->value.GetBool();
            if (m_context->editor_camera_entity) {
                const auto id = *m_context->editor_camera_entity;
                if (doc.HasMember("camera") && doc["camera"].IsObject())
                    if (auto camera = m_context->registry->get<commons::camera>(id)) camera->analyze(doc["camera"]);
                if (doc.HasMember("transform") && doc["transform"].IsObject()) {
                    const auto& v = doc["transform"];
                    bool valid = true;
                    for (auto key : {"position", "rotation", "scale"}) {
                        if (!v.HasMember(key) || !v[key].IsArray() || v[key].Size() != 3) { valid = false; break; }
                        for (auto& number : v[key].GetArray())
                            if (!number.IsNumber() || !std::isfinite(number.GetFloat())) valid = false;
                    }
                    if (valid) if (auto transform = m_context->registry->get<commons::transform>(id)) transform->analyze(v);
                }
            }
            return true;
        } catch (const std::exception& e) { error = e.what(); return false; }
    }
    ui_persistence_system& m_persistence;
    std::filesystem::path m_root;
    std::filesystem::path m_session_path;
    std::filesystem::path m_layout_path;
};

}
