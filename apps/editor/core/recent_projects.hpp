#pragma once
#include "project_file_transaction.hpp"
#include <fstream>
#include <vector>
#include <optional>

namespace editor::ui {
class recent_projects {
public:
    explicit recent_projects(std::filesystem::path path) : m_path(std::filesystem::absolute(path).lexically_normal()) {}
    std::vector<std::string> list() const {
        std::vector<std::string> result;
        std::ifstream input(m_path);
        for (std::string line; std::getline(input, line);) if (!line.empty()) result.push_back(line);
        return result;
    }
    std::optional<std::string> last() const {
        for (const auto& path : list()) {
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error) && !error) return path;
        }
        return {};
    }
    bool remember(const std::string& path, std::string& error) const {
        if (!project_files::recover(m_path, error)) return false;
        std::error_code io_error;
        if (std::filesystem::exists(m_path, io_error) && !std::ifstream(m_path)) {
            error = "Could not read recent projects; list preserved."; return false;
        }
        if (io_error) { error = io_error.message(); return false; }
        const auto absolute = std::filesystem::absolute(path).lexically_normal().string();
        std::string contents = absolute + '\n';
        std::size_t count = 1;
        for (const auto& previous : list()) {
            if (previous != absolute && count < 12) { contents += previous + '\n'; ++count; }
        }
        return project_files::save(m_path, {{m_path, contents}}, error);
    }
private:
    std::filesystem::path m_path;
};
}
