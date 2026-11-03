#pragma once

#include "util/vertex.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace commons {

struct editable_face_group {
    std::string name;
    std::vector<unsigned int> faces;
};

struct editable_mesh_data {
    std::vector<vertex> vertices;
    std::vector<std::vector<unsigned int>> faces;
    std::string material_path;
    std::vector<editable_face_group> face_groups;

    bool validate(std::string& error) const;
    std::vector<unsigned int> triangulated_indices() const;
    void recalculate_normals();
};

struct editable_model_data {
    std::vector<editable_mesh_data> meshes;

    bool validate(std::string& error) const;
};

bool load_editable_model(
    const std::filesystem::path& path,
    editable_model_data& result,
    std::string& error
);
bool save_editable_model(
    const std::filesystem::path& path,
    const editable_model_data& model,
    std::string& error
);

}
