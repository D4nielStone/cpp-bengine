#pragma once

#include <filesystem>
#include <string>

namespace editor {
// Empty path with an empty error means the user cancelled.
std::filesystem::path browse_native(const std::filesystem::path& initial,
                                    bool directory, std::string& error);
}
