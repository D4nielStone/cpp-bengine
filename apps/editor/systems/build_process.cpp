#include "build_process.hpp"

#include <fstream>
#include <cstdlib>
#include <functional>
#include <string>
#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace editor::ui {
namespace {

const char* target_name(const build_target target) {
    return target == build_target::windows ? "windows" : "linux";
}

#ifdef _WIN32
std::wstring quote_argument(const std::wstring& value) {
    std::wstring result{L"\""};
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(character);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}
#endif

}

build_process::~build_process() {
#ifdef _WIN32
    if (m_process_handle)
        CloseHandle(static_cast<HANDLE>(m_process_handle));
#endif
}

bool build_process::start(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& build_directory,
    const build_target target,
    std::string& error)
{
    return start(repository_root, build_directory, target, {}, error);
}

bool build_process::start(
    const std::filesystem::path& repository_root,
    const std::filesystem::path& build_directory,
    const build_target target,
    const std::filesystem::path& project_file,
    std::string& error)
{
    std::cout << "[Build] Starting platform build" << std::endl;
    std::cout << "[Build] Repository root: '" << repository_root.string() << "'" << std::endl;
    std::cout << "[Build] Output directory: '" << build_directory.string() << "'" << std::endl;
    std::cout << "[Build] Target: '" << target_name(target) << "'" << std::endl;

    if (build_directory.empty()) {
        error = "Choose a build output directory.";
        std::cout << "[Build] Rejected: output directory is empty" << std::endl;
        return false;
    }

    if (running(error)) {
        if (error.empty())
            error = "A build is already running.";
        std::cout << "[Build] Rejected: another build is running: " << error << std::endl;
        return false;
    }

#ifdef _WIN32
    const auto script = repository_root / "scripts" /
        (project_file.empty() ? "compile_runtime.ps1" : "platform_build.ps1");
#else
    const auto script = repository_root / "scripts" /
        (project_file.empty() ? "compile_runtime.sh" : "platform_build.sh");
#endif
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(script, filesystem_error)) {
        error = "Platform build script not found: " + script.string();
        return false;
    }
    auto workspace = build_directory;
    if (!project_file.empty()) {
        const char* cache = std::getenv(
#ifdef _WIN32
            "LOCALAPPDATA"
#else
            "XDG_CACHE_HOME"
#endif
        );
        auto cache_root = cache && *cache ? std::filesystem::path(cache) : std::filesystem::temp_directory_path();
        const auto key = repository_root.string() + "|" + project_file.string() + "|" + build_directory.string() + "|" + target_name(target);
        workspace = cache_root / "cpp-bengine" / "game-builds" / std::to_string(std::hash<std::string>{}(key));
    }
    std::filesystem::create_directories(workspace, filesystem_error);
    if (filesystem_error) {
        error = "Could not create build directory: " + filesystem_error.message();
        return false;
    }

    const std::string name = target_name(target);
    m_log_path = workspace / ("editor-build-" + name + ".log");
    m_progress_path = workspace / ("editor-build-" + name + ".progress");
    {
        std::ofstream progress_file(m_progress_path, std::ios::trunc);
        if (!progress_file) {
            error = "Could not create build progress file '" +
                m_progress_path.string() + "'.";
            return false;
        }
        progress_file << "0|Starting platform build...\n";
        if (!progress_file) {
            error = "Could not initialize build progress file '" +
                m_progress_path.string() + "'.";
            return false;
        }
    }
    m_exit_code = -1;
#ifdef _WIN32
    SECURITY_ATTRIBUTES security_attributes{};
    security_attributes.nLength = sizeof(security_attributes);
    security_attributes.bInheritHandle = TRUE;
    HANDLE log_file = CreateFileW(
        m_log_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ,
        &security_attributes,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (log_file == INVALID_HANDLE_VALUE) {
        error = "Could not open build log '" + m_log_path.string() +
            "' (Windows error " + std::to_string(GetLastError()) + ").";
        return false;
    }

    const auto& wrapper = script;
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    startup_info.dwFlags = STARTF_USESTDHANDLES;
    startup_info.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = log_file;
    startup_info.hStdError = log_file;
    PROCESS_INFORMATION process_info{};
    std::wstring command =
        L"powershell.exe -NoLogo -NoProfile -NonInteractive "
        L"-ExecutionPolicy Bypass -File " +
        quote_argument(wrapper.wstring()) +
        L" -Target " + std::wstring(name.begin(), name.end()) +
        L" -RepositoryRoot " + quote_argument(repository_root.wstring()) +
        L" -BuildDirectory " + quote_argument(build_directory.wstring()) +
        L" -ProgressFile " + quote_argument(m_progress_path.wstring()) +
        (project_file.empty() ? L"" : L" -ProjectFile " + quote_argument(project_file.wstring()) +
            L" -Workspace " + quote_argument(workspace.wstring()));
    const BOOL started = CreateProcessW(
        nullptr,
        command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workspace.c_str(),
        &startup_info,
        &process_info
    );
    CloseHandle(log_file);
    if (!started) {
        error = "Could not start CMake build (Windows error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process_info.hThread);
    m_process_handle = process_info.hProcess;
#else
    const int log_file = open(
        m_log_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC,
        0644
    );
    if (log_file < 0) {
        error = "Could not open build log '" + m_log_path.string() +
            "': " + std::strerror(errno);
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        error = "Could not start CMake build: " +
            std::string(std::strerror(errno));
        close(log_file);
        return false;
    }
    if (child == 0) {
        if (dup2(log_file, STDOUT_FILENO) < 0 ||
            dup2(log_file, STDERR_FILENO) < 0 ||
            chdir(repository_root.c_str()) < 0) {
            dprintf(log_file, "Could not prepare CMake build process: %s\n",
                std::strerror(errno));
            _exit(127);
        }
        close(log_file);
        const auto script_path = script.string();
        const auto build_path = build_directory.string();
        const auto progress_path = m_progress_path.string();
        execl(
            "/bin/sh",
            "sh",
            script_path.c_str(),
            name.c_str(),
            repository_root.c_str(),
            build_path.c_str(),
            progress_path.c_str(),
            project_file.c_str(),
            workspace.c_str(),
            static_cast<char*>(nullptr)
        );
        dprintf(STDERR_FILENO, "Could not start platform build: %s\n",
            std::strerror(errno));
        _exit(127);
    }
    close(log_file);
    m_process_id = static_cast<int>(child);
#endif
    return true;
}

bool build_process::running(std::string& error) {
    error.clear();
#ifdef _WIN32
    if (!m_process_handle)
        return false;

    const HANDLE process_handle = static_cast<HANDLE>(m_process_handle);
    const DWORD wait_result = WaitForSingleObject(process_handle, 0);
    if (wait_result == WAIT_TIMEOUT)
        return true;
    if (wait_result != WAIT_OBJECT_0) {
        error = "Could not inspect CMake build process (Windows error " +
            std::to_string(GetLastError()) + ").";
        return true;
    }

    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process_handle, &exit_code)) {
        error = "Could not read CMake build exit code (Windows error " +
            std::to_string(GetLastError()) + ").";
        return true;
    }
    m_exit_code = static_cast<int>(exit_code);
    CloseHandle(process_handle);
    m_process_handle = nullptr;
#else
    if (m_process_id < 0)
        return false;

    int status = 0;
    const pid_t result = waitpid(
        static_cast<pid_t>(m_process_id),
        &status,
        WNOHANG
    );
    if (result == 0)
        return true;
    if (result < 0) {
        if (errno == EINTR)
            return true;
        error = "Could not inspect CMake build process: " +
            std::string(std::strerror(errno));
        return true;
    }

    m_exit_code = WIFEXITED(status)
        ? WEXITSTATUS(status)
        : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 1);
    m_process_id = -1;
#endif
    return false;
}

int build_process::exit_code() const noexcept {
    return m_exit_code;
}

const std::filesystem::path& build_process::log_path() const noexcept {
    return m_log_path;
}

float build_process::progress(std::string& status) const {
    std::ifstream file(m_progress_path);
    if (!file)
        return 0.f;

    float value = 0.f;
    if (!(file >> value) || file.get() != '|' ||
        !std::getline(file, status)) {
        status = "Preparing platform build...";
        return 0.f;
    }
    return value;
}

}
