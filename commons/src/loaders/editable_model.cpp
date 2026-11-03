#include "loaders/editable_model.hpp"

#include <os/resource_files.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>

namespace commons {
namespace {

constexpr std::size_t max_meshes = 4096;
constexpr std::size_t max_vertices = 4'000'000;
constexpr std::size_t max_faces = 4'000'000;
constexpr std::size_t max_face_vertices = 1'000'000;

bool finite(const bgui::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
        std::isfinite(value.z);
}

bool read_float_array(
    const rapidjson::Value& value,
    const std::size_t count,
    float* output
) {
    if (!value.IsArray() || value.Size() != count)
        return false;
    for (std::size_t index = 0; index < count; ++index) {
        if (!value[index].IsNumber())
            return false;
        const auto component = value[index].GetFloat();
        if (!std::isfinite(component))
            return false;
        output[index] = component;
    }
    return true;
}

bool parse_mesh(
    const rapidjson::Value& value,
    editable_mesh_data& mesh,
    std::string& error
) {
    if (!value.IsObject() || !value.HasMember("vertices") ||
        !value["vertices"].IsArray() || !value.HasMember("faces") ||
        !value["faces"].IsArray()) {
        error = "Malformed editable mesh data.";
        return false;
    }

    if (value.HasMember("material")) {
        if (!value["material"].IsString() || value["material"].GetStringLength() != std::strlen(value["material"].GetString())) {
            error = "Invalid native material reference."; return false;
        }
        mesh.material_path = value["material"].GetString();
    }
    const auto& vertices = value["vertices"];
    const auto& faces = value["faces"];
    if (vertices.Empty() || vertices.Size() > max_vertices ||
        faces.Empty() || faces.Size() > max_faces) {
        error = "Editable mesh exceeds supported limits.";
        return false;
    }

    mesh.vertices.reserve(vertices.Size());
    for (const auto& source : vertices.GetArray()) {
        float attributes[5]{};
        if (!read_float_array(source, 5, attributes)) {
            error = "Editable mesh contains an invalid vertex.";
            return false;
        }
        mesh.vertices.emplace_back(
            bgui::vec3{attributes[0], attributes[1], attributes[2]},
            bgui::vec3{},
            bgui::vec2{attributes[3], attributes[4]}
        );
    }

    mesh.faces.reserve(faces.Size());
    for (const auto& source : faces.GetArray()) {
        if (!source.IsArray() || source.Size() < 3 ||
            source.Size() > max_face_vertices) {
            error = "Editable mesh contains an invalid face.";
            return false;
        }
        auto& face = mesh.faces.emplace_back();
        face.reserve(source.Size());
        for (const auto& index : source.GetArray()) {
            if (!index.IsUint() || index.GetUint() >= mesh.vertices.size()) {
                error = "Editable mesh face references an invalid vertex.";
                return false;
            }
            face.push_back(index.GetUint());
        }
    }
    if (value.HasMember("face_groups")) {
        const auto& groups = value["face_groups"];
        if (!groups.IsArray() || groups.Size() > max_faces) {
            error = "Invalid face groups."; return false;
        }
        for (const auto& group : groups.GetArray()) {
            if (!group.IsObject() || !group.HasMember("name") || !group["name"].IsString() ||
                !group.HasMember("faces") || !group["faces"].IsArray() || group["faces"].Size() > max_faces) {
                error = "Invalid face group."; return false;
            }
            editable_face_group loaded;
            loaded.name.assign(group["name"].GetString(), group["name"].GetStringLength());
            for (const auto& index : group["faces"].GetArray()) {
                if (!index.IsUint() || index.GetUint() >= mesh.faces.size()) {
                    error = "Face group references an invalid face."; return false;
                }
                loaded.faces.push_back(index.GetUint());
            }
            mesh.face_groups.push_back(std::move(loaded));
        }
    }
    mesh.recalculate_normals();
    return mesh.validate(error);
}

}

bool editable_mesh_data::validate(std::string& error) const {
    if (vertices.empty() || vertices.size() > max_vertices ||
        faces.empty() || faces.size() > max_faces) {
        error = "Editable mesh must contain vertices and faces.";
        return false;
    }
    for (const auto& value : vertices) {
        if (!finite(value.position) || !std::isfinite(value.uvcoords.x) ||
            !std::isfinite(value.uvcoords.y)) {
            error = "Editable mesh contains non-finite vertex data.";
            return false;
        }
    }
    for (const auto& face : faces) {
        if (face.size() < 3 || face.size() > max_face_vertices) {
            error = "Editable mesh contains a face with invalid size.";
            return false;
        }
        for (const auto index : face) {
            if (index >= vertices.size()) {
                error = "Editable mesh face references an invalid vertex.";
                return false;
            }
        }
    }
    for (const auto& group : face_groups) {
        if (group.name.empty() || group.faces.empty()) {
            error = "Face group must have a name and faces."; return false;
        }
        for (auto face : group.faces) if (face >= faces.size()) {
            error = "Face group references an invalid face."; return false;
        }
    }
    return true;
}

std::vector<unsigned int> editable_mesh_data::triangulated_indices() const {
    std::vector<unsigned int> indices;
    for (const auto& face : faces) {
        for (std::size_t corner = 1; corner + 1 < face.size(); ++corner) {
            indices.push_back(face[0]);
            indices.push_back(face[corner]);
            indices.push_back(face[corner + 1]);
        }
    }
    return indices;
}

void editable_mesh_data::recalculate_normals() {
    for (auto& value : vertices)
        value.normal = {};
    for (const auto& face : faces) {
        if (face.size() < 3)
            continue;
        const auto& origin = vertices[face[0]].position;
        for (std::size_t corner = 1; corner + 1 < face.size(); ++corner) {
            const auto first = vertices[face[corner]].position - origin;
            const auto second = vertices[face[corner + 1]].position - origin;
            const bgui::vec3 normal{
                first.y * second.z - first.z * second.y,
                first.z * second.x - first.x * second.z,
                first.x * second.y - first.y * second.x
            };
            for (const auto index : face)
                vertices[index].normal = vertices[index].normal + normal;
        }
    }
    for (auto& value : vertices) {
        const float length = std::sqrt(
            value.normal.x * value.normal.x +
            value.normal.y * value.normal.y +
            value.normal.z * value.normal.z
        );
        if (length > std::numeric_limits<float>::epsilon())
            value.normal = value.normal / length;
    }
}

bool editable_model_data::validate(std::string& error) const {
    if (meshes.empty() || meshes.size() > max_meshes) {
        error = "Editable model must contain at least one mesh.";
        return false;
    }
    for (const auto& mesh : meshes)
        if (!mesh.validate(error))
            return false;
    return true;
}

bool load_editable_model(
    const std::filesystem::path& path,
    editable_model_data& result,
    std::string& error
) {
    error.clear();
    auto input = bgui::resource_files::stream(path);
    if (!input) {
        error = "Could not open editable model '" + path.string() + "'.";
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    if (!input.eof() && input.fail()) {
        error = "Could not read editable model '" + path.string() + "'.";
        return false;
    }

    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError() || !document.IsObject() ||
        !document.HasMember("format") || !document["format"].IsString() ||
        std::string_view(document["format"].GetString()) != "bengine-model" ||
        !document.HasMember("version") || !document["version"].IsInt() ||
        document["version"].GetInt() != 1 ||
        !document.HasMember("meshes") || !document["meshes"].IsArray() ||
        document["meshes"].Empty() ||
        document["meshes"].Size() > max_meshes) {
        error = "Editable model is malformed or has an unsupported version.";
        return false;
    }

    editable_model_data loaded;
    loaded.meshes.reserve(document["meshes"].Size());
    for (const auto& source : document["meshes"].GetArray()) {
        editable_mesh_data mesh;
        if (!parse_mesh(source, mesh, error))
            return false;
        loaded.meshes.push_back(std::move(mesh));
    }
    if (!loaded.validate(error))
        return false;
    result = std::move(loaded);
    return true;
}

bool save_editable_model(
    const std::filesystem::path& path,
    const editable_model_data& model,
    std::string& error
) {
    error.clear();
    if (path.empty()) {
        error = "Choose a destination for the editable model.";
        return false;
    }
    if (!model.validate(error))
        return false;

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.StartObject();
    writer.Key("format");
    writer.String("bengine-model");
    writer.Key("version");
    writer.Int(1);
    writer.Key("meshes");
    writer.StartArray();
    for (const auto& mesh : model.meshes) {
        writer.StartObject();
        if (!mesh.material_path.empty()) { writer.Key("material"); writer.String(mesh.material_path.c_str()); }
        writer.Key("vertices");
        writer.StartArray();
        for (const auto& value : mesh.vertices) {
            writer.StartArray();
            writer.Double(value.position.x);
            writer.Double(value.position.y);
            writer.Double(value.position.z);
            writer.Double(value.uvcoords.x);
            writer.Double(value.uvcoords.y);
            writer.EndArray();
        }
        writer.EndArray();
        writer.Key("faces");
        writer.StartArray();
        for (const auto& face : mesh.faces) {
            writer.StartArray();
            for (const auto index : face)
                writer.Uint(index);
            writer.EndArray();
        }
        writer.EndArray();
        writer.Key("face_groups");
        writer.StartArray();
        for (const auto& group : mesh.face_groups) {
            writer.StartObject();
            writer.Key("name"); writer.String(group.name.c_str());
            writer.Key("faces"); writer.StartArray();
            for (auto face : group.faces) writer.Uint(face);
            writer.EndArray(); writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    {
        std::ifstream existing(path, std::ios::binary | std::ios::ate);
        if (existing && existing.tellg() == std::streamoff(buffer.GetSize())) {
            std::string contents(buffer.GetSize(), '\0');
            if (existing.seekg(0).read(contents.data(), contents.size()) &&
                contents.compare(0, contents.size(), buffer.GetString(), buffer.GetSize()) == 0)
                return true;
        }
    }

    static std::atomic_uint64_t next_temp{0};
    auto temporary = path;
    temporary += ".tmp." + std::to_string(next_temp.fetch_add(1));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "Could not create editable model temporary file '" +
                temporary.string() + "'.";
            return false;
        }
        output.write(buffer.GetString(),
            static_cast<std::streamsize>(buffer.GetSize()));
        output.flush();
        if (!output) {
            error = "Could not write editable model temporary file '" +
                temporary.string() + "'.";
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return false;
        }
    }

    std::error_code filesystem_error;
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        error = "Could not replace editable model '" + path.string() +
            "': " + filesystem_error.message();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }
    return true;
}

}
