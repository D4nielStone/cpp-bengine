#pragma once

#include <filesystem>
#include <string>

namespace editor::ui {

class runtime_process final {
public:
    runtime_process() = default;
    ~runtime_process();

    runtime_process(const runtime_process&) = delete;
    runtime_process& operator=(const runtime_process&) = delete;

    bool start(
        const std::filesystem::path& executable,
        const std::filesystem::path& project,
        std::string& error
    );
    bool running(std::string& error);
    void collect_script_logs();
    bool set_paused(bool paused, std::string& error);
    bool stop(std::string& error);

private:
    void cleanup_project_file();

    std::filesystem::path m_project_file;
    std::filesystem::path m_log_file;
    std::uintmax_t m_log_offset{0};
    bool m_is_paused{false};
#ifdef _WIN32
    void* m_process_handle{nullptr};
#else
    int m_process_id{-1};
#endif
};

}
