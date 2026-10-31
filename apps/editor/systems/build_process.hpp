#pragma once

#include <filesystem>
#include <string>

namespace editor::ui {

enum class build_target {
    linux,
    windows
};

struct game_export_options {
    bool single_executable{false};
    std::filesystem::path runtime_template;
};

class build_process final {
public:
    build_process() = default;
    ~build_process();

    build_process(const build_process&) = delete;
    build_process& operator=(const build_process&) = delete;

    bool start(
        const std::filesystem::path& repository_root,
        const std::filesystem::path& build_directory,
        build_target target,
        std::string& error
    );
    bool start(
        const std::filesystem::path& repository_root,
        const std::filesystem::path& output_directory,
        build_target target,
        const std::filesystem::path& project_file,
        std::string& error
    );
    bool start(const std::filesystem::path& repository_root,
        const std::filesystem::path& output_directory, build_target target,
        const std::filesystem::path& project_file, const game_export_options& options,
        std::string& error);
    bool running(std::string& error);
    int exit_code() const noexcept;
    const std::filesystem::path& log_path() const noexcept;
    float progress(std::string& status) const;

private:
    std::filesystem::path m_log_path;
    std::filesystem::path m_progress_path;
    int m_exit_code{-1};
#ifdef _WIN32
    void* m_process_handle{nullptr};
#else
    int m_process_id{-1};
#endif
};

}
