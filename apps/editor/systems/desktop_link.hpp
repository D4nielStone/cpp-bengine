#pragma once
#include <string>
#include <string_view>
#include <cctype>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace editor::ui {
inline bool is_desktop_link(std::string_view target) {
    for (unsigned char ch : target) if (ch < 32 || ch == 127) return false;
    auto scheme = std::string(target.substr(0, target.find(':')));
    for (auto& ch : scheme) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return (scheme == "http" || scheme == "https" || scheme == "mailto") && target.find(':') != std::string_view::npos;
}
inline void open_desktop_link(const std::string& target) {
    if (!is_desktop_link(target)) return;
    const auto* url = target.c_str();
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    // Double fork lets the desktop opener outlive the editor without a zombie.
    const auto child = fork();
    if (child == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", url, static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    if (child > 0) while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
#endif
}
}
