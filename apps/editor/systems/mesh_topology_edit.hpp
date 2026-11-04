#pragma once

#include "loaders/editable_model.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace editor::ui {

using mesh_edge = std::pair<unsigned int, unsigned int>;

inline mesh_edge ordered_mesh_edge(
    const unsigned int first,
    const unsigned int second
) {
    return std::minmax(first, second);
}

struct mesh_element_clipboard {
    commons::editable_mesh_data geometry;
    std::set<mesh_edge> edges;
};

inline mesh_element_clipboard copy_mesh_elements(
    const commons::editable_mesh_data& mesh,
    const std::set<std::size_t>& faces,
    const std::set<std::size_t>& vertices,
    const std::set<mesh_edge>& edges
) {
    mesh_element_clipboard copy;
    std::map<unsigned int, unsigned int> vertex_map;
    std::map<unsigned int, unsigned int> face_map;
    const auto copy_vertex = [&](unsigned int index) {
        const auto [entry, added] = vertex_map.try_emplace(index, copy.geometry.vertices.size());
        if (added) copy.geometry.vertices.push_back(mesh.vertices[index]);
        return entry->second;
    };

    for (auto index : vertices)
        if (index < mesh.vertices.size()) copy_vertex(index);

    if (faces.empty() && edges.empty()) return copy;
    for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
        const auto& face = mesh.faces[index];
        bool selected = faces.contains(index);
        for (std::size_t corner = 0; !edges.empty() && corner < face.size(); ++corner) {
            const auto edge = ordered_mesh_edge(face[corner], face[(corner + 1) % face.size()]);
            if (edges.contains(edge)) {
                const auto first = copy_vertex(edge.first);
                const auto second = copy_vertex(edge.second);
                copy.edges.insert(ordered_mesh_edge(first, second));
                selected = true;
            }
        }
        if (!selected) continue;
        face_map.emplace(index, copy.geometry.faces.size());
        auto& polygon = copy.geometry.faces.emplace_back();
        for (auto vertex : face) polygon.push_back(copy_vertex(vertex));
    }

    for (const auto& group : mesh.face_groups) {
        commons::editable_face_group copied_group{group.name, {}};
        for (auto index : group.faces)
            if (const auto found = face_map.find(index); found != face_map.end())
                copied_group.faces.push_back(found->second);
        if (!copied_group.faces.empty()) copy.geometry.face_groups.push_back(std::move(copied_group));
    }
    return copy;
}

inline bool append_mesh_elements(commons::editable_mesh_data& mesh,
    const mesh_element_clipboard& clipboard, std::string& error
) {
    const auto& geometry = clipboard.geometry;
    if (geometry.vertices.empty()) return false;
    auto result = mesh;
    const auto vertex_offset = static_cast<unsigned int>(result.vertices.size());
    const auto face_offset = static_cast<unsigned int>(result.faces.size());
    result.vertices.insert(result.vertices.end(), geometry.vertices.begin(), geometry.vertices.end());
    for (auto face : geometry.faces) {
        for (auto& index : face) index += vertex_offset;
        result.faces.push_back(std::move(face));
    }
    for (auto group : geometry.face_groups) {
        for (auto& index : group.faces) index += face_offset;
        result.face_groups.push_back(std::move(group));
    }
    if (!result.validate(error)) return false;
    result.recalculate_normals();
    mesh = std::move(result);
    return true;
}

inline bool delete_mesh_elements(
    commons::editable_mesh_data& mesh,
    const std::set<std::size_t>& faces,
    const std::set<std::size_t>& vertices,
    const std::set<mesh_edge>& edges,
    std::string& error
) {
    error.clear();
    if (!mesh.validate(error)) return false;

    auto result = mesh;
    result.faces.clear();
    std::vector<std::optional<unsigned int>> face_map(mesh.faces.size());
    std::vector<bool> removed_vertices(mesh.vertices.size());
    std::vector<bool> used_vertices(mesh.vertices.size());
    bool changed = false;
    for (const auto vertex : vertices) {
        if (vertex >= mesh.vertices.size()) continue;
        removed_vertices[vertex] = true;
        changed = true;
    }
    for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
        const auto& face = mesh.faces[index];
        bool remove = faces.contains(index);
        for (std::size_t corner = 0; corner < face.size(); ++corner) {
            const auto first = face[corner], second = face[(corner + 1) % face.size()];
            remove |= vertices.contains(first) || edges.contains(ordered_mesh_edge(first, second));
        }
        if (remove) {
            changed = true;
            for (const auto vertex : face) removed_vertices[vertex] = true;
        } else {
            face_map[index] = static_cast<unsigned int>(result.faces.size());
            result.faces.push_back(face);
            for (const auto vertex : face) used_vertices[vertex] = true;
        }
    }
    if (!changed) return false;

    result.vertices.clear();
    std::vector<unsigned int> vertex_map(mesh.vertices.size());
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        if (removed_vertices[index] && !used_vertices[index]) continue;
        vertex_map[index] = static_cast<unsigned int>(result.vertices.size());
        result.vertices.push_back(mesh.vertices[index]);
    }
    for (auto& face : result.faces)
        for (auto& vertex : face) vertex = vertex_map[vertex];

    for (auto& group : result.face_groups) {
        std::vector<unsigned int> remaining;
        for (const auto face : group.faces)
            if (face_map[face]) remaining.push_back(*face_map[face]);
        group.faces = std::move(remaining);
    }
    std::erase_if(result.face_groups, [](const auto& group) { return group.faces.empty(); });
    result.recalculate_normals();
    if (!result.validate(error)) return false;
    mesh = std::move(result);
    return true;
}

inline bgui::vec3 face_normal(
    const commons::editable_mesh_data& mesh,
    const std::vector<unsigned int>& face
) {
    bgui::vec3 normal{};
    if (face.size() < 3)
        return normal;
    const auto& origin = mesh.vertices[face[0]].position;
    for (std::size_t corner = 1; corner + 1 < face.size(); ++corner) {
        const auto first = mesh.vertices[face[corner]].position - origin;
        const auto second = mesh.vertices[face[corner + 1]].position - origin;
        normal.x += first.y * second.z - first.z * second.y;
        normal.y += first.z * second.x - first.x * second.z;
        normal.z += first.x * second.y - first.y * second.x;
    }
    const auto length = std::sqrt(
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z
    );
    if (length > 1e-6f)
        normal = normal / length;
    return normal;
}

inline bool extrude_faces(
    commons::editable_mesh_data& mesh,
    const std::set<std::size_t>& selected_faces,
    const float distance,
    std::string& error
) {
    error.clear();
    if (!mesh.validate(error)) return false;
    if (selected_faces.empty() || !std::isfinite(distance) ||
        std::abs(distance) < 1e-6f) {
        error = "Selecione faces e informe uma distância de extrusão válida.";
        return false;
    }
    for (const auto face : selected_faces) {
        if (face >= mesh.faces.size()) {
            error = "A seleção de faces ficou desatualizada.";
            return false;
        }
    }

    struct edge_info {
        unsigned int first{0};
        unsigned int second{0};
        std::size_t selected_uses{0};
    };
    std::map<mesh_edge, edge_info> edges;
    std::map<unsigned int, bgui::vec3> vertex_normals;
    for (const auto face_index : selected_faces) {
        const auto& face = mesh.faces[face_index];
        const auto normal = face_normal(mesh, face);
        for (const auto vertex : face)
            vertex_normals[vertex] = vertex_normals[vertex] + normal;
        for (std::size_t corner = 0; corner < face.size(); ++corner) {
            const auto first = face[corner];
            const auto second = face[(corner + 1) % face.size()];
            auto& edge = edges[ordered_mesh_edge(first, second)];
            if (edge.selected_uses == 0) {
                edge.first = first;
                edge.second = second;
            }
            ++edge.selected_uses;
        }
    }

    auto result = mesh;
    std::map<unsigned int, unsigned int> extruded_vertices;
    for (const auto& [source, normal] : vertex_normals) {
        auto direction = normal;
        const auto length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y +
            direction.z * direction.z
        );
        if (length > 1e-6f)
            direction = direction / length;
        auto value = mesh.vertices[source];
        value.position = value.position + direction * distance;
        extruded_vertices.emplace(
            source,
            static_cast<unsigned int>(result.vertices.size())
        );
        result.vertices.push_back(value);
    }

    std::vector<unsigned int> face_map(mesh.faces.size());
    std::vector<std::vector<unsigned int>> faces;
    faces.reserve(mesh.faces.size() + selected_faces.size() + edges.size());
    for (std::size_t index = 0; index < mesh.faces.size(); ++index) {
        if (!selected_faces.contains(index)) {
            face_map[index] = static_cast<unsigned int>(faces.size());
            faces.push_back(mesh.faces[index]);
        }
    }
    for (const auto face_index : selected_faces) {
        face_map[face_index] = static_cast<unsigned int>(faces.size());
        auto top = mesh.faces[face_index];
        for (auto& vertex : top)
            vertex = extruded_vertices.at(vertex);
        faces.push_back(std::move(top));
    }
    for (const auto& [key, edge] : edges) {
        (void)key;
        if (edge.selected_uses != 1)
            continue;
        faces.push_back({
            edge.first,
            edge.second,
            extruded_vertices.at(edge.second),
            extruded_vertices.at(edge.first)
        });
    }
    for (auto& group : result.face_groups)
        for (auto& face : group.faces) face = face_map[face];
    result.faces = std::move(faces);
    result.recalculate_normals();
    if (!result.validate(error))
        return false;
    mesh = std::move(result);
    return true;
}

inline void slide_loop_cut(commons::editable_mesh_data& mesh, std::size_t first_new_vertex,
    const std::vector<mesh_edge>& rails, float fraction) {
    for (std::size_t i = 0; i < rails.size(); ++i) {
        const auto [a, b] = rails[i];
        auto& vertex = mesh.vertices[first_new_vertex + i];
        const auto& first = mesh.vertices[a];
        const auto& second = mesh.vertices[b];
        vertex.position = first.position + (second.position - first.position) * fraction;
        vertex.uvcoords = first.uvcoords + (second.uvcoords - first.uvcoords) * fraction;
    }
    mesh.recalculate_normals();
}

inline bool loop_cut(
    commons::editable_mesh_data& mesh,
    const mesh_edge& selected_edge,
    std::string& error,
    std::vector<mesh_edge>* slide_rails = nullptr
) {
    error.clear();
    if (!mesh.validate(error)) return false;
    const auto seed = ordered_mesh_edge(selected_edge.first, selected_edge.second);
    if (seed.first == seed.second || seed.second >= mesh.vertices.size()) {
        error = "Selecione uma aresta válida para aplicar Loop Cut.";
        return false;
    }

    std::map<mesh_edge, mesh_edge> cut_edges{{seed, selected_edge}};
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& face : mesh.faces) {
            std::vector<std::size_t> matching_edges;
            for (std::size_t corner = 0; corner < face.size(); ++corner) {
                if (cut_edges.contains(ordered_mesh_edge(
                        face[corner], face[(corner + 1) % face.size()])))
                    matching_edges.push_back(corner);
            }
            if (matching_edges.empty())
                continue;
            if (face.size() != 4 || matching_edges.size() > 2 ||
                (matching_edges.size() == 2 &&
                 (matching_edges[0] + 2) % 4 != matching_edges[1])) {
                error = "Loop Cut requer um loop contínuo de faces quadriláteras.";
                return false;
            }
            if (matching_edges.size() == 2)
                continue;
            const auto opposite = (matching_edges.front() + 2) % 4;
            const auto corner = matching_edges.front();
            const auto direction = cut_edges.at(ordered_mesh_edge(face[corner], face[(corner + 1) % 4]));
            mesh_edge rail{face[(opposite + 1) % 4], face[opposite]};
            if (direction.first != face[corner]) std::swap(rail.first, rail.second);
            changed = cut_edges.emplace(ordered_mesh_edge(rail.first, rail.second), rail).second || changed;
        }
    }

    std::map<mesh_edge, unsigned int> midpoint_indices;
    auto result = mesh;
    std::vector<mesh_edge> rails;
    for (const auto& [edge, rail] : cut_edges) {
        rails.push_back(rail);
        const auto& first = mesh.vertices[edge.first];
        const auto& second = mesh.vertices[edge.second];
        commons::vertex midpoint;
        midpoint.position = (first.position + second.position) * 0.5f;
        midpoint.normal = (first.normal + second.normal) * 0.5f;
        midpoint.uvcoords = (first.uvcoords + second.uvcoords) * 0.5f;
        midpoint_indices.emplace(
            edge,
            static_cast<unsigned int>(result.vertices.size())
        );
        result.vertices.push_back(midpoint);
    }

    std::vector<std::vector<unsigned int>> split_faces;
    split_faces.reserve(mesh.faces.size() * 2);
    std::vector<std::vector<unsigned int>> face_map(mesh.faces.size());
    for (std::size_t face_index = 0; face_index < mesh.faces.size(); ++face_index) {
        const auto& face = mesh.faces[face_index];
        face_map[face_index].push_back(static_cast<unsigned int>(split_faces.size()));
        if (face.size() != 4) {
            split_faces.push_back(face);
            continue;
        }
        std::vector<std::size_t> cuts;
        for (std::size_t corner = 0; corner < 4; ++corner) {
            if (cut_edges.contains(ordered_mesh_edge(
                    face[corner], face[(corner + 1) % 4])))
                cuts.push_back(corner);
        }
        if (cuts.empty()) {
            split_faces.push_back(face);
            continue;
        }
        if (cuts.size() != 2 ||
            (cuts[0] + 2) % 4 != cuts[1]) {
            error = "Loop Cut encontrou uma face sem duas arestas opostas.";
            return false;
        }
        face_map[face_index].push_back(static_cast<unsigned int>(split_faces.size() + 1));
        const auto first_cut = cuts[0];
        const auto opposite_cut = cuts[1];
        const auto midpoint_a = midpoint_indices.at(ordered_mesh_edge(
            face[first_cut], face[(first_cut + 1) % 4]
        ));
        const auto midpoint_b = midpoint_indices.at(ordered_mesh_edge(
            face[opposite_cut], face[(opposite_cut + 1) % 4]
        ));
        split_faces.push_back({
            midpoint_a,
            face[(first_cut + 1) % 4],
            face[(first_cut + 2) % 4],
            midpoint_b
        });
        split_faces.push_back({
            midpoint_b,
            face[(opposite_cut + 1) % 4],
            face[(opposite_cut + 2) % 4],
            midpoint_a
        });
    }
    if (split_faces.size() == mesh.faces.size()) {
        error = "A aresta selecionada não pertence a um loop quadrilátero.";
        return false;
    }
    for (auto& group : result.face_groups) {
        std::vector<unsigned int> mapped;
        for (auto face : group.faces) mapped.insert(mapped.end(), face_map[face].begin(), face_map[face].end());
        group.faces = std::move(mapped);
    }
    result.faces = std::move(split_faces);
    result.recalculate_normals();
    if (!result.validate(error))
        return false;
    mesh = std::move(result);
    if (slide_rails) *slide_rails = std::move(rails);
    return true;
}

}
