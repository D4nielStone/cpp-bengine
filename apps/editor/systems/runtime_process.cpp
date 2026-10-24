#include "runtime_process.hpp"

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <vector>

namespace editor::ui {
namespace {

#ifdef _WIN32
bool set_process_threads_paused(
    const DWORD process_id,
    const bool paused,
    std::string& error
) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        error = "Could not inspect runtime process threads (Windows error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }

    std::vector<DWORD> changed_threads;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    bool success = true;
    if (!Thread32First(snapshot, &entry)) {
        error = "Could not enumerate runtime process threads (Windows error " +
            std::to_string(GetLastError()) + ").";
        success = false;
    }

    while (success) {
        if (entry.th32OwnerProcessID == process_id) {
            const HANDLE thread = OpenThread(
                THREAD_SUSPEND_RESUME,
                FALSE,
                entry.th32ThreadID
            );
            if (!thread) {
                error = "Could not access runtime process thread (Windows error " +
                    std::to_string(GetLastError()) + ").";
                success = false;
                break;
            }

            const DWORD result = paused
                ? SuspendThread(thread)
                : ResumeThread(thread);
            if (result == static_cast<DWORD>(-1)) {
                error = std::string("Could not ") +
                    (paused ? "pause" : "resume") +
                    " runtime process thread (Windows error " +
                    std::to_string(GetLastError()) + ").";
                success = false;
            } else {
                changed_threads.push_back(entry.th32ThreadID);
            }
            CloseHandle(thread);
            if (!success)
                break;
        }

        entry.dwSize = sizeof(entry);
        if (!Thread32Next(snapshot, &entry)) {
            if (GetLastError() != ERROR_NO_MORE_FILES) {
                error = "Could not enumerate runtime process threads (Windows error " +
                    std::to_string(GetLastError()) + ").";
                success = false;
            }
            break;
        }
    }

    CloseHandle(snapshot);
    if (success)
        return true;

    for (const DWORD thread_id : changed_threads) {
        const HANDLE thread = OpenThread(
            THREAD_SUSPEND_RESUME,
            FALSE,
            thread_id
        );
        if (!thread)
            continue;
        if (paused)
            ResumeThread(thread);
        else
            SuspendThread(thread);
        CloseHandle(thread);
    }
    return false;
}
#endif

}

runtime_process::~runtime_process() {
    std::string error;
    if (!stop(error))
        std::cerr << "[editor] Could not stop runtime process: " << error << '\n';
}

bool runtime_process::start(
    const std::filesystem::path& executable,
    const std::filesystem::path& project,
    std::string& error
) {
    error.clear();
    if (running(error)) {
        error = "The game runtime is already running.";
        return false;
    }
    if (!error.empty())
        return false;

    m_project_file = project;
    m_is_paused = false;

#ifdef _WIN32
    std::wstring command = L"\"" + executable.wstring() +
        L"\" --project \"" + project.wstring() + L"\"";
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info{};
    if (!CreateProcessW(
            executable.c_str(),
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info
        )) {
        error = "Could not start runtime process (Windows error " +
            std::to_string(GetLastError()) + ").";
        cleanup_project_file();
        return false;
    }
    CloseHandle(process_info.hThread);
    m_process_handle = process_info.hProcess;
#else
    int exec_status[2]{};
    if (pipe(exec_status) != 0) {
        error = "Could not create runtime startup pipe: " +
            std::string(std::strerror(errno));
        cleanup_project_file();
        return false;
    }
    if (fcntl(exec_status[1], F_SETFD, FD_CLOEXEC) < 0) {
        error = "Could not configure runtime startup pipe: " +
            std::string(std::strerror(errno));
        close(exec_status[0]);
        close(exec_status[1]);
        cleanup_project_file();
        return false;
    }

    const pid_t child = fork();
    if (child < 0) {
        error = "Could not fork runtime process: " +
            std::string(std::strerror(errno));
        close(exec_status[0]);
        close(exec_status[1]);
        cleanup_project_file();
        return false;
    }

    if (child == 0) {
        close(exec_status[0]);
        execl(
            executable.c_str(),
            executable.c_str(),
            "--project",
            project.c_str(),
            static_cast<char*>(nullptr)
        );
        const int exec_error = errno;
        (void)write(exec_status[1], &exec_error, sizeof(exec_error));
        _exit(127);
    }

    close(exec_status[1]);
    int exec_error = 0;
    ssize_t bytes_read;
    do {
        bytes_read = read(exec_status[0], &exec_error, sizeof(exec_error));
    } while (bytes_read < 0 && errno == EINTR);
    close(exec_status[0]);
    if (bytes_read != 0) {
        if (bytes_read < 0) {
            error = "Could not verify runtime startup: " +
                std::string(std::strerror(errno));
        } else {
            error = "Could not start runtime executable '" +
                executable.string() + "': " +
                std::string(std::strerror(exec_error));
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        cleanup_project_file();
        return false;
    }

    m_process_id = static_cast<int>(child);
#endif
    return true;
}

bool runtime_process::running(std::string& error) {
    error.clear();
#ifdef _WIN32
    if (!m_process_handle)
        return false;

    const DWORD result = WaitForSingleObject(
        static_cast<HANDLE>(m_process_handle),
        0
    );
    if (result == WAIT_TIMEOUT)
        return true;
    if (result != WAIT_OBJECT_0)
    {
        error = "Could not check runtime process state (Windows error " +
            std::to_string(GetLastError()) + ").";
        return true;
    }

    CloseHandle(static_cast<HANDLE>(m_process_handle));
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
    if (result < 0 && errno != EINTR) {
        error = "Could not check runtime process state: " +
            std::string(std::strerror(errno));
        return true;
    }
    if (result < 0)
        return true;

    m_process_id = -1;
#endif
    m_is_paused = false;
    cleanup_project_file();
    return false;
}

bool runtime_process::set_paused(
    const bool paused,
    std::string& error
) {
    error.clear();
    if (paused == m_is_paused)
        return true;

    if (!running(error)) {
        if (error.empty())
            error = "The game runtime is not running.";
        return false;
    }

#ifdef _WIN32
    if (!set_process_threads_paused(
            GetProcessId(static_cast<HANDLE>(m_process_handle)),
            paused,
            error
        )) {
        return false;
    }
#else
    const int signal = paused ? SIGSTOP : SIGCONT;
    if (kill(static_cast<pid_t>(m_process_id), signal) != 0) {
        error = "Could not " + std::string(paused ? "pause" : "resume") +
            " runtime process: " + std::string(std::strerror(errno));
        return false;
    }
#endif

    m_is_paused = paused;
    return true;
}

bool runtime_process::stop(std::string& error) {
    error.clear();
    const bool process_running = running(error);
    if (!error.empty())
        return false;
    if (!process_running)
        return true;
    if (m_is_paused && !set_paused(false, error))
        return false;

#ifdef _WIN32
    const auto process = static_cast<HANDLE>(m_process_handle);
    if (!TerminateProcess(process, 0)) {
        error = "Could not stop runtime process (Windows error " +
            std::to_string(GetLastError()) + ").";
        return false;
    }
    WaitForSingleObject(process, INFINITE);
    CloseHandle(process);
    m_process_handle = nullptr;
#else
    const pid_t process = static_cast<pid_t>(m_process_id);
    if (kill(process, SIGTERM) != 0 && errno != ESRCH) {
        error = "Could not stop runtime process: " +
            std::string(std::strerror(errno));
        return false;
    }

    int status = 0;
    while (waitpid(process, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        if (errno != ECHILD) {
            error = "Could not wait for runtime process: " +
                std::string(std::strerror(errno));
            return false;
        }
        break;
    }
    m_process_id = -1;
#endif
    m_is_paused = false;
    cleanup_project_file();
    return true;
}

void runtime_process::cleanup_project_file() {
    if (m_project_file.empty())
        return;

    std::error_code error;
    const auto scene_file = m_project_file.parent_path() /
        (m_project_file.stem().string() + ".scene");
    std::filesystem::remove(scene_file, error);
    if (error)
        std::cerr << "[editor] Could not remove runtime scene file '"
                  << scene_file.string() << "': " << error.message() << '\n';

    error.clear();
    std::filesystem::remove(m_project_file, error);
    if (error)
        std::cerr << "[editor] Could not remove runtime project file '"
                  << m_project_file.string() << "': " << error.message() << '\n';
    m_project_file.clear();
}

}
