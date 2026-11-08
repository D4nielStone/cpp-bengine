#pragma once
#include <filesystem>
#include <functional>
namespace editor::ui {
class panel_manager;
class files_view;
struct scene_workflow_actions { std::function<void()> scenes, scripting; };
scene_workflow_actions install_scene_workflow(panel_manager& scenes, panel_manager& scripts, files_view&,
    std::function<std::filesystem::path()> root,
    std::function<void(const std::filesystem::path&)> open_scene,
    std::function<bool()> editable, std::function<void()> activate_scripting = {});
// The panel manager owns each window controller; document buffers remain in Files.
std::function<void()> install_scene_workflow(panel_manager&, files_view&,
    std::function<std::filesystem::path()> root,
    std::function<void(const std::filesystem::path&)> open_scene,
    std::function<bool()> editable);
}
