#pragma once

#include "loaders/editable_model.hpp"

#include <algorithm>
#include <cmath>
#include <map>
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

inline bool loop_cut(
    commons::editable_mesh_data& mesh,
    const mesh_edge& selected_edge,
    std::string& error
) {
    error.clear();
    if (!mesh.validate(error)) return false;
    const auto seed = ordered_mesh_edge(selected_edge.first, selected_edge.second);
    if (seed.first == seed.second || seed.second >= mesh.vertices.size()) {
        error = "Selecione uma aresta válida para aplicar Loop Cut.";
        return false;
    }

    std::set<mesh_edge> cut_edges{seed};
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
            const auto edge = ordered_mesh_edge(
                face[opposite],
                face[(opposite + 1) % 4]
            );
            changed = cut_edges.insert(edge).second || changed;
        }
    }

    std::map<mesh_edge, unsigned int> midpoint_indices;
    auto result = mesh;
    for (const auto& edge : cut_edges) {
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
    return true;
}

}
