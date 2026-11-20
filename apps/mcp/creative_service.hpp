#pragma once
#include "editor/systems/runtime_process.hpp"
#include <rapidjson/document.h>
#include <filesystem>
#include <chrono>

namespace bubble::mcp {
class creative_service {
public:
    creative_service(std::filesystem::path project, std::filesystem::path workspace,
        std::filesystem::path runtime, unsigned editor_port = 0);
    rapidjson::Value call(std::string_view name, const rapidjson::Value& arguments,
        rapidjson::Document::AllocatorType& memory);
    const std::filesystem::path& project() const { return m_project; }
    std::filesystem::path capture() const;
    bool connected() const { return m_editor_port != 0; }
    rapidjson::Document live_snapshot();
private:
    std::filesystem::path m_project, m_workspace, m_runtime, m_test;
    unsigned m_editor_port{};
    rapidjson::Document live_request(std::string_view action, const rapidjson::Value* data = nullptr);
    editor::ui::runtime_process m_process;
    std::chrono::steady_clock::time_point m_deadline;
    bool m_timed_out{false};
    std::filesystem::path path(const std::string& relative) const;
    void write(const std::filesystem::path& target, const std::string& contents,
        const rapidjson::Value& arguments);
};
}
