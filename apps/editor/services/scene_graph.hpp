#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace editor::ui {
struct scene_graph_node { std::filesystem::path path; float x{}, y{}; std::vector<std::filesystem::path> scripts; };
struct scene_graph_edge { std::size_t from{}, to{}; std::filesystem::path script; std::string target; bool unresolved{}; };
struct scene_graph {
    std::vector<scene_graph_node> nodes;
    std::vector<scene_graph_edge> edges;
    std::vector<std::string> diagnostics;
};
std::vector<std::filesystem::path> project_scripts(const std::filesystem::path& root);
scene_graph read_scene_graph(const std::filesystem::path& root,
    const std::function<std::string(const std::filesystem::path&)>& read = {});
}
