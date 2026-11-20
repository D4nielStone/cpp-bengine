#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace editor::ui {
struct editor_context;
class project_persistence_system;
struct live_document_hooks {
    std::function<bool(const std::filesystem::path&)> dirty;
    std::function<void(const std::filesystem::path&)> reload;
};
std::string dispatch_live_scene(std::string_view request, std::string_view token,
    const std::string& project, editor_context& context, project_persistence_system& persistence,
    const live_document_hooks& documents);
}
