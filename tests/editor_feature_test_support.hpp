#pragma once
#include "editor_features.hpp"
#include <chrono>
#include <cstdlib>
#include <optional>

class scoped_editor_features {
    std::optional<std::string> previous;
    std::filesystem::path directory;
    static void environment(const char* value) {
#ifdef _WIN32
        _putenv_s("BENGINE_INSTALLATION", value ? value : "");
#else
        if (value) setenv("BENGINE_INSTALLATION", value, 1);
        else unsetenv("BENGINE_INSTALLATION");
#endif
    }
public:
    explicit scoped_editor_features(bool enabled) {
        if (auto* value = std::getenv("BENGINE_INSTALLATION")) previous = value;
        directory = std::filesystem::temp_directory_path() / ("bengine-feature-test-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory);
        environment(directory.string().c_str());
        if (enabled) editor::ui::save_visual_scripting_enabled(true);
    }
    ~scoped_editor_features() {
        environment(previous ? previous->c_str() : nullptr);
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
