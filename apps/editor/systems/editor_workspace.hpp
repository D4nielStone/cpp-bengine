#pragma once

#include "ui_persistence_system.hpp"
#include "files_view.hpp"

#include <filesystem>
#include <string>

namespace editor::ui {

class editor_workspace final {
public:
    explicit editor_workspace(ui_persistence_system& persistence)
        : m_persistence(persistence) {}

    void set_project_file(const std::filesystem::path& project_file) {
        std::error_code error;
        auto absolute_path = std::filesystem::absolute(project_file, error);
        m_root = error ? project_file.parent_path() : absolute_path.parent_path();
        m_session_path = m_root / ".editor-session.cfg";
        m_layout_path = m_root / ".editor-ui.cfg";
    }

    bool load_workspace(files_view& files, std::string& error) const {
        error.clear();
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
        if (!files.save_session(m_session_path)) {
            error = "Could not save open documents to '" +
                m_session_path.string() + "'.";
            return false;
        }
        return m_persistence.save(m_layout_path.string(), error);
    }

private:
    ui_persistence_system& m_persistence;
    std::filesystem::path m_root;
    std::filesystem::path m_session_path;
    std::filesystem::path m_layout_path;
};

}
