#pragma once
#include <cstdlib>
#include <filesystem>

namespace editor::distribution {
inline std::filesystem::path user_path(const char* relative) {
#ifdef _WIN32
    const auto* installation = _wgetenv(L"BENGINE_INSTALLATION");
#else
    const auto* installation = std::getenv("BENGINE_INSTALLATION");
#endif
    if (installation && *installation)
        return std::filesystem::path(installation) / relative;
    return relative;
}

// The installed manager supplies the immutable version directory. Development
// builds retain their CMake-provided paths.
inline std::filesystem::path path(const char* installed, const char* development) {
#ifdef _WIN32
    const auto* home = _wgetenv(L"BENGINE_HOME");
#else
    const auto* home = std::getenv("BENGINE_HOME");
#endif
    if (home && *home)
        return std::filesystem::path(home) / installed;
    return development;
}
}
