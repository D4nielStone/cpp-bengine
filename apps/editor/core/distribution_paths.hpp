#pragma once
#include <cstdlib>
#include <filesystem>

namespace editor::distribution {
// The installed manager supplies the immutable version directory. Development
// builds retain their CMake-provided paths.
inline std::filesystem::path path(const char* installed, const char* development) {
    const char* home = std::getenv("BENGINE_HOME");
    if (home && *home)
        return std::filesystem::path(home) / installed;
    return development;
}
}
