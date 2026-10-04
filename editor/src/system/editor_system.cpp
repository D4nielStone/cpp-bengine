#include "system/editor_system.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "components/camera.hpp"
#include "components/directional_light.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "debugging/debug.hpp"
#include "elem/menu_bar.hpp"
#include "system/editor_ui_elements.hpp"
#include "system/project_assets.hpp"

#include <bgui.hpp>
#include <elem/input_area.hpp>
#include <lay/dock.hpp>
#include <rapidjson/prettywriter.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif

namespace {
    bool write_file_atomically(const std::filesystem::path& path, const std::string& contents) {
        static std::atomic_uint64_t temporary_sequence{0};
        const auto temporary_path = path.string() + ".tmp." +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
            std::to_string(temporary_sequence.fetch_add(1, std::memory_order_relaxed));

        {
            std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            output.flush();
            if (!output.good()) {
                output.close();
                std::error_code ignored;
                std::filesystem::remove(temporary_path, ignored);
                return false;
            }
            output.close();
            if (output.fail()) {
                std::error_code ignored;
                std::filesystem::remove(temporary_path, ignored);
                return false;
            }
        }

#ifdef _WIN32
        if (!MoveFileExW(
                std::filesystem::path(temporary_path).c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
            return false;
        }
#else
        std::error_code error;
        std::filesystem::rename(temporary_path, path, error);
        if (error) {
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
            return false;
        }
#endif
        return true;
    }

#ifdef _WIN32
    std::string utf8_path(const wchar_t* path) {
        if (!path || !*path)
            return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
            return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), length, nullptr, nullptr);
        result.pop_back();
        return result;
    }

    std::string choose_windows_dialog(
        const bool save,
        const bool directory,
        const bool model,
        const bool project)
    {
        const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool uninitialize = SUCCEEDED(initialize_result);
        if (FAILED(initialize_result) && initialize_result != RPC_E_CHANGED_MODE)
            return {};

        IFileDialog* dialog = nullptr;
        const auto dialog_class = save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog;
        HRESULT result = CoCreateInstance(
            dialog_class,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&dialog)
        );
        if (FAILED(result)) {
            if (uninitialize)
                CoUninitialize();
            return {};
        }

        const wchar_t* title = directory
            ? L"Criar projeto Bubble"
            : model
                ? L"Importar modelo 3D"
                : project
                    ? L"Abrir projeto"
                    : save
                        ? L"Salvar cena"
                        : L"Importar cena";
        dialog->SetTitle(title);

        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
            if (directory) {
                options |= FOS_PICKFOLDERS;
            } else if (save) {
                options |= FOS_OVERWRITEPROMPT;
            } else {
                options |= FOS_FILEMUSTEXIST;
            }
            dialog->SetOptions(options);
        }

        if (!directory) {
            const COMDLG_FILTERSPEC model_filter[] = {
                {L"Modelos 3D", L"*.obj;*.dae;*.fbx;*.gltf;*.glb;*.stl;*.ply"},
                {L"Todos os arquivos", L"*.*"}
            };
            const COMDLG_FILTERSPEC project_filter[] = {
                {L"Projeto Bubble", L"*.bproject"},
                {L"Todos os arquivos", L"*.*"}
            };
            const COMDLG_FILTERSPEC scene_filter[] = {
                {L"Cenas Bubble", L"*.bscene"},
                {L"Todos os arquivos", L"*.*"}
            };
            const COMDLG_FILTERSPEC* filters = model ? model_filter : project ? project_filter : scene_filter;
            dialog->SetFileTypes(2, filters);
            if (save && !project && !model)
                dialog->SetDefaultExtension(L"bscene");
        }

        std::string selected_path;
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    selected_path = utf8_path(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }

        dialog->Release();
        if (uninitialize)
            CoUninitialize();
        return selected_path;
    }
#endif

    std::filesystem::path recent_project_file() {
#ifdef _WIN32
        if (const char* app_data = std::getenv("APPDATA"))
            return std::filesystem::path(app_data) / "BubbleEngine" / "recent_project.txt";
#else
        if (const char* config_home = std::getenv("XDG_CONFIG_HOME"))
            return std::filesystem::path(config_home) / "BubbleEngine" / "recent_project.txt";
        if (const char* home = std::getenv("HOME"))
            return std::filesystem::path(home) / ".config" / "BubbleEngine" / "recent_project.txt";
#endif
        return std::filesystem::current_path() / "recent_project.txt";
    }

    bool remember_recent_project(const std::filesystem::path& project_path) {
        const auto recent_path = recent_project_file();
        std::error_code error;
        std::filesystem::create_directories(recent_path.parent_path(), error);
        if (error)
            return false;

        return write_file_atomically(
            recent_path,
            std::filesystem::absolute(project_path).lexically_normal().string());
    }

    std::string load_recent_project() {
        std::ifstream input(recent_project_file(), std::ios::binary);
        if (!input)
            return {};

        std::string path((std::istreambuf_iterator<char>(input)), {});
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
        return path;
    }

    std::string choose_file(const bool save, const bool model, const bool project = false) {
#ifdef _WIN32
        return choose_windows_dialog(save, false, model, project);
#else
        const char* command = model
            ? "zenity --file-selection --title='Importar modelo 3D' --file-filter='Modelos 3D | *.obj *.dae *.fbx *.gltf *.glb *.stl *.ply' 2>/dev/null"
            : project
                ? "zenity --file-selection --title='Abrir projeto' --file-filter='Projeto Bubble | *.bproject' 2>/dev/null"
            : save
                ? "zenity --file-selection --save --confirm-overwrite --title='Salvar cena' --file-filter='Cenas | *.bscene' 2>/dev/null"
                : "zenity --file-selection --title='Importar cena' --file-filter='Cenas | *.bscene' 2>/dev/null";
        std::FILE* dialog = popen(command, "r");
        if (!dialog)
            return {};

        std::string path;
        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), dialog))
            path += buffer.data();
        const int result = pclose(dialog);
        if (result != 0)
            return {};
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
        return path;
#endif
    }

    std::string choose_project_directory() {
#ifdef _WIN32
        return choose_windows_dialog(false, true, false, false);
#else
        std::FILE* dialog = popen("zenity --file-selection --directory --title='Criar projeto Bubble' 2>/dev/null", "r");
        if (!dialog)
            return {};
        std::string path;
        std::array<char, 4096> buffer{};
        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), dialog))
            path += buffer.data();
        const int result = pclose(dialog);
        if (result != 0)
            return {};
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
            path.pop_back();
        return path;
#endif
    }

    std::string normalized_key(const std::string& key) {
        std::string normalized;
        for (const unsigned char character : key) {
            if (std::isalnum(character))
                normalized.push_back(static_cast<char>(std::tolower(character)));
        }
        return normalized;
    }

    const rapidjson::Value* member_value(
        const rapidjson::Value& object,
        const std::string& normalized_name)
    {
        if (!object.IsObject())
            return nullptr;
        for (auto member = object.MemberBegin(); member != object.MemberEnd(); ++member) {
            if (normalized_key(member->name.GetString()) == normalized_name)
                return &member->value;
        }
        return nullptr;
    }

    bool read_vector(const rapidjson::Value& object, const char* key, COMMONS_NS::fvec3& result) {
        const auto* value = member_value(object, key);
        if (!value)
            return false;
        if (value->IsArray() && value->Size() >= 3 &&
            (*value)[0].IsNumber() && (*value)[1].IsNumber() && (*value)[2].IsNumber()) {
            result = {(*value)[0].GetFloat(), (*value)[1].GetFloat(), (*value)[2].GetFloat()};
            return true;
        }
        if (value->IsObject()) {
            const auto* x = member_value(*value, "x");
            const auto* y = member_value(*value, "y");
            const auto* z = member_value(*value, "z");
            if (x && y && z && x->IsNumber() && y->IsNumber() && z->IsNumber()) {
                result = {x->GetFloat(), y->GetFloat(), z->GetFloat()};
                return true;
            }
        }
        return false;
    }

    bool read_float(const rapidjson::Value& object, const char* key, float& result) {
        const auto* value = member_value(object, key);
        if (!value || !value->IsNumber())
            return false;
        result = value->GetFloat();
        return std::isfinite(result);
    }

    bool read_float_array(
        const rapidjson::Value& object,
        const char* key,
        float* result,
        const rapidjson::SizeType count)
    {
        const auto* value = member_value(object, key);
        if (!value || !value->IsArray() || value->Size() != count)
            return false;
        for (rapidjson::SizeType index = 0; index < count; ++index) {
            if (!(*value)[index].IsNumber())
                return false;
            result[index] = (*value)[index].GetFloat();
            if (!std::isfinite(result[index]))
                return false;
        }
        return true;
    }

    const rapidjson::Value* scene_transform(
        const rapidjson::Value& object,
        const rapidjson::Value* inherited)
    {
        if (const auto* components = member_value(object, "components")) {
            if (const auto* transform = member_value(*components, "transform"))
                return transform;
        }
        if (const auto* transform = member_value(object, "transform"))
            return transform;
        if (member_value(object, "position") || member_value(object, "rotation") || member_value(object, "scale"))
            return &object;
        return inherited;
    }

    bool is_ignored_gameplay_object(const rapidjson::Value& object) {
        for (const char* key : {"type", "kind", "category", "class"}) {
            const auto* value = member_value(object, key);
            if (!value || !value->IsString())
                continue;
            const auto type = normalized_key(value->GetString());
            if (type == "player" || type == "enemy" || type == "enemyarea" || type == "area")
                return true;
        }
        return false;
    }

    std::string model_path(const rapidjson::Value& object, const bool model_context) {
        for (const char* key : {"model", "modelpath", "mesh", "meshpath", "modelfile"}) {
            const auto* value = member_value(object, key);
            if (value && value->IsString())
                return value->GetString();
        }
        if (const auto* renderer = member_value(object, "renderer")) {
            if (renderer->IsString())
                return renderer->GetString();
            if (renderer->IsObject()) {
                for (const char* key : {"model", "modelpath", "mesh", "meshpath", "path", "file", "directory"}) {
                    const auto* value = member_value(*renderer, key);
                    if (value && value->IsString())
                        return value->GetString();
                }
            }
        }
        if (model_context) {
            for (const char* key : {"path", "file", "directory", "source"}) {
                const auto* value = member_value(object, key);
                if (value && value->IsString())
                    return value->GetString();
            }
        }
        return {};
    }

    std::string relative_asset_path(const std::filesystem::path& path) {
        return path.lexically_relative(std::filesystem::path(COMMONS_ASSET_DIR)).generic_string();
    }

    std::string stable_path_key(const std::filesystem::path& path) {
        uint64_t hash = 14695981039346656037ull;
        for (const unsigned char character : path.generic_string()) {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        std::ostringstream output;
        output << std::hex << std::setw(16) << std::setfill('0') << hash;
        return output.str();
    }

    bool collect_gltf_uris(
        const rapidjson::Value& value,
        const std::filesystem::path& base_directory,
        std::vector<std::filesystem::path>& dependencies)
    {
        if (value.IsArray()) {
            for (const auto& child : value.GetArray()) {
                if (!collect_gltf_uris(child, base_directory, dependencies))
                    return false;
            }
            return true;
        }
        if (!value.IsObject())
            return true;

        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const auto key = normalized_key(member->name.GetString());
            if (key == "uri" && member->value.IsString()) {
                const std::string uri = member->value.GetString();
                if (uri.rfind("data:", 0) == 0)
                    continue;
                if (uri.find("://") != std::string::npos)
                    return false;
                const std::filesystem::path dependency(uri);
                dependencies.push_back(dependency.is_absolute() ? dependency : base_directory / dependency);
            } else if (!collect_gltf_uris(member->value, base_directory, dependencies)) {
                return false;
            }
        }
        return true;
    }

    bool collect_model_dependencies(
        const std::filesystem::path& model_path,
        std::vector<std::filesystem::path>& dependencies)
    {
        std::vector<std::filesystem::path> pending{model_path};
        std::unordered_set<std::string> visited;
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            std::error_code error;
            current = std::filesystem::absolute(current, error).lexically_normal();
            if (error || !std::filesystem::is_regular_file(current, error) || error ||
                std::filesystem::is_symlink(std::filesystem::symlink_status(current, error)) || error)
                return false;

            const auto identity = current.generic_string();
            if (!visited.insert(identity).second)
                continue;
            dependencies.push_back(current);

            const auto extension = normalized_key(current.extension().string());
            if (extension == ".obj") {
                std::ifstream input(current, std::ios::binary);
                if (!input)
                    return false;
                std::string line;
                while (std::getline(input, line)) {
                    std::istringstream fields(line);
                    std::string keyword;
                    fields >> keyword;
                    if (keyword != "mtllib")
                        continue;
                    std::string material;
                    bool found_material = false;
                    while (fields >> material) {
                        pending.push_back(current.parent_path() / material);
                        found_material = true;
                    }
                    if (!found_material)
                        return false;
                }
                if (!input.eof())
                    return false;
            } else if (extension == ".mtl") {
                std::ifstream input(current, std::ios::binary);
                if (!input)
                    return false;
                std::string line;
                while (std::getline(input, line)) {
                    std::istringstream fields(line);
                    std::string keyword;
                    fields >> keyword;
                    if (keyword.rfind("map_", 0) != 0 && keyword != "bump" &&
                        keyword != "disp" && keyword != "decal" && keyword != "norm" &&
                        keyword != "refl")
                        continue;
                    std::string token;
                    std::string filename;
                    while (fields >> token)
                        filename = token;
                    if (filename.empty())
                        return false;
                    pending.push_back(current.parent_path() / filename);
                }
                if (!input.eof())
                    return false;
            } else if (extension == ".gltf") {
                std::ifstream input(current, std::ios::binary);
                if (!input)
                    return false;
                const std::string contents((std::istreambuf_iterator<char>(input)), {});
                rapidjson::Document document;
                document.Parse(contents.c_str());
                if (document.HasParseError() ||
                    !collect_gltf_uris(document, current.parent_path(), pending))
                    return false;
            }
        }

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(model_path.string(), 0);
        if (!scene || !scene->HasMeshes())
            return false;
        for (unsigned int material_index = 0; material_index < scene->mNumMaterials; ++material_index) {
            const auto* material = scene->mMaterials[material_index];
            for (int texture_type = aiTextureType_NONE; texture_type <= aiTextureType_UNKNOWN; ++texture_type) {
                const auto type = static_cast<aiTextureType>(texture_type);
                for (unsigned int texture_index = 0; texture_index < material->GetTextureCount(type); ++texture_index) {
                    aiString texture_path;
                    if (material->GetTexture(type, texture_index, &texture_path) != AI_SUCCESS)
                        return false;
                    const std::string referenced = texture_path.C_Str();
                    if (referenced.empty() || referenced[0] == '*')
                        continue;
                    const std::filesystem::path dependency(referenced);
                    pending.push_back(dependency.is_absolute()
                        ? dependency
                        : model_path.parent_path() / dependency);
                }
            }
        }

        for (const auto& dependency : pending) {
            std::error_code error;
            const auto absolute = std::filesystem::absolute(dependency, error).lexically_normal();
            if (error || !std::filesystem::is_regular_file(absolute, error) || error ||
                std::filesystem::is_symlink(std::filesystem::symlink_status(absolute, error)) || error)
                return false;
            const auto identity = absolute.generic_string();
            if (visited.insert(identity).second)
                dependencies.push_back(absolute);
        }
        return true;
    }

    bool package_scene_assets_impl(
        const std::filesystem::path& scene_path,
        const std::filesystem::path& project_root)
    {
        std::ifstream input(scene_path, std::ios::binary);
        if (!input)
            return false;
        const std::string contents((std::istreambuf_iterator<char>(input)), {});
        rapidjson::Document document;
        document.Parse(contents.c_str());
        if (document.HasParseError() || !document.IsObject())
            return false;

        const auto project_assets = project_root / "Assets";
        const auto scene_directory = scene_path.parent_path();
        const auto package_model = [&](rapidjson::Value& model) {
            if (!model.IsString())
                return true;

            const std::filesystem::path requested(model.GetString());
            std::vector<std::filesystem::path> candidates;
            if (requested.is_absolute()) {
                candidates.push_back(requested);
            } else {
                candidates = {
                    scene_directory / requested,
                    project_root / requested,
                    project_assets / requested,
                    std::filesystem::path(COMMONS_ASSET_DIR) / requested,
                    std::filesystem::path(COMMONS_MODEL_ASSET_DIR) / requested
                };
            }

            std::error_code error;
            std::filesystem::path source;
            for (const auto& candidate : candidates) {
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error)) {
                    source = std::filesystem::absolute(candidate, error).lexically_normal();
                    if (!error)
                        break;
                    source.clear();
                }
            }
            if (source.empty())
                return false;

            error.clear();
            const auto absolute_assets = std::filesystem::absolute(project_assets, error);
            if (error)
                return false;
            const auto relative_source = source.lexically_relative(absolute_assets);
            if (relative_source.empty() || *relative_source.begin() == "..") {
                std::vector<std::filesystem::path> dependencies;
                if (!collect_model_dependencies(source, dependencies))
                    return false;

                auto source_root = source.parent_path();
                for (const auto& dependency : dependencies) {
                    auto relative = dependency.lexically_relative(source_root);
                    while (relative.empty() || *relative.begin() == "..") {
                        const auto parent = source_root.parent_path();
                        if (parent == source_root || parent.empty())
                            return false;
                        source_root = parent;
                        relative = dependency.lexically_relative(source_root);
                    }
                }

                const auto destination_directory = project_assets / "Models" /
                    stable_path_key(source);
                for (const auto& dependency : dependencies) {
                    const auto relative_file = dependency.lexically_relative(source_root);
                    if (relative_file.empty() || *relative_file.begin() == "..")
                        return false;
                    const auto destination = destination_directory / relative_file;
                    std::filesystem::create_directories(destination.parent_path(), error);
                    if (error)
                        return false;
                    std::filesystem::copy_file(
                        dependency, destination, std::filesystem::copy_options::overwrite_existing, error);
                    if (error)
                        return false;
                }
                const auto model_relative = source.lexically_relative(source_root);
                if (model_relative.empty() || *model_relative.begin() == "..")
                    return false;
                const auto project_relative = (std::filesystem::path("Assets") / "Models" /
                    destination_directory.filename() / model_relative).generic_string();
                model.SetString(project_relative.c_str(), static_cast<rapidjson::SizeType>(project_relative.size()), document.GetAllocator());
            } else {
                const auto project_relative = relative_source.generic_string();
                model.SetString(project_relative.c_str(), static_cast<rapidjson::SizeType>(project_relative.size()), document.GetAllocator());
            }
            return true;
        };

        const auto process_value = [&](auto&& self, rapidjson::Value& value) -> bool {
            if (value.IsArray()) {
                for (auto& child : value.GetArray()) {
                    if (!self(self, child))
                        return false;
                }
                return true;
            }
            if (!value.IsObject())
                return true;

            for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
                const auto key = normalized_key(member->name.GetString());
                const bool is_model_reference = key == "model" || key == "modelpath" || key == "mesh" ||
                    key == "meshpath" || key == "modelfile";
                if (is_model_reference && member->value.IsString()) {
                    if (!package_model(member->value))
                        return false;
                } else if (key == "renderer" && member->value.IsString()) {
                    if (!package_model(member->value))
                        return false;
                } else if (!self(self, member->value)) {
                    return false;
                }
            }
            return true;
        };

        if (!process_value(process_value, document))
            return false;

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        std::ofstream output(scene_path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
        return output.good();
    }

    void import_models(
        const rapidjson::Value& value,
        const rapidjson::Value* inherited_transform,
        const bool model_context,
        const std::filesystem::path& scene_directory,
        const std::filesystem::path& project_assets,
        const std::shared_ptr<COMMONS_NS::ecs>& registry,
        std::size_t& imported)
    {
        if (value.IsArray()) {
            for (const auto& child : value.GetArray())
                import_models(child, inherited_transform, model_context, scene_directory, project_assets, registry, imported);
            return;
        }
        if (!value.IsObject() || is_ignored_gameplay_object(value))
            return;

        const auto* transform = scene_transform(value, inherited_transform);
        const auto path = model_path(value, model_context);
        if (!path.empty()) {
            std::filesystem::path resolved_model(path);
            std::error_code error;
            if (resolved_model.is_relative()) {
                const auto relative_to_assets = std::filesystem::path(COMMONS_ASSET_DIR) / resolved_model;
                if (std::filesystem::is_regular_file(relative_to_assets, error)) {
                    resolved_model = relative_to_assets;
                } else if (!project_assets.empty() &&
                    std::filesystem::is_regular_file(project_assets.parent_path() / resolved_model, error)) {
                    resolved_model = project_assets.parent_path() / resolved_model;
                } else if (!project_assets.empty() && std::filesystem::is_regular_file(project_assets / resolved_model, error)) {
                    resolved_model = project_assets / resolved_model;
                } else if (!std::filesystem::is_regular_file(resolved_model, error)) {
                    const auto relative_to_scene = scene_directory / resolved_model;
                    if (std::filesystem::is_regular_file(relative_to_scene, error))
                        resolved_model = relative_to_scene;
                }
            }

            auto entity = registry->create();
            registry->add<COMMONS_NS::renderer>(entity, resolved_model.string().c_str());
            auto renderer = registry->get<COMMONS_NS::renderer>(entity.id);
            if (renderer && renderer->m_modelo && !renderer->m_modelo->meshes.empty()) {
                if (transform) {
                    auto entity_transform = registry->get<COMMONS_NS::transform>(entity.id);
                    auto position = entity_transform->get_position();
                    auto rotation = entity_transform->get_rotation();
                    auto scale = entity_transform->get_scale();
                    read_vector(*transform, "position", position);
                    read_vector(*transform, "rotation", rotation);
                    read_vector(*transform, "scale", scale);
                    entity_transform->set_position(position);
                    entity_transform->set_rotation(rotation);
                    entity_transform->set_scale(scale);
                }
                ++imported;
            } else {
                registry->remove(entity.id);
            }
        }

        for (auto member = value.MemberBegin(); member != value.MemberEnd(); ++member) {
            const auto key = normalized_key(member->name.GetString());
            const bool is_model_reference = key == "renderer" || key == "model" ||
                key == "modelpath" || key == "mesh" || key == "meshpath" || key == "modelfile";
            if (!path.empty() && is_model_reference)
                continue;
            const bool child_model_context = model_context || key == "models" || key == "model" ||
                key == "meshes" || key == "mesh" || key == "renderer";
            import_models(member->value, transform, child_model_context, scene_directory, project_assets, registry, imported);
        }
    }
}

bool editor::project_assets::package_scene_assets(
    const std::filesystem::path& scene_path,
    const std::filesystem::path& project_root)
{
    return package_scene_assets_impl(scene_path, project_root);
}

editor::editor_system::~editor_system() {
    if (auto registry = m_registry.lock(); registry && !m_project_config_path.empty())
        save_editor_cache(registry);
    m_config.save_interface();
}

float editor::editor_system::camera_move_speed() const {
    return m_config.camera_move_speed();
}

float editor::editor_system::camera_look_sensitivity() const {
    return m_config.camera_look_sensitivity();
}

float editor::editor_system::camera_zoom_sensitivity() const {
    return m_config.camera_zoom_sensitivity();
}

float editor::editor_system::ui_scale() const {
    return m_config.ui_scale();
}

float editor::editor_system::camera_min_z_far() const {
    return m_camera_min_z_far;
}

bool editor::editor_system::grid_enabled() const {
    return m_grid_gizmo.enabled;
}

float editor::editor_system::grid_spacing() const {
    return m_grid_gizmo.spacing;
}

float editor::editor_system::grid_extent() const {
    return m_grid_gizmo.extent;
}

void editor::editor_system::set_camera_move_speed(const float value) {
    m_config.set_camera_move_speed(value);
}

void editor::editor_system::set_camera_look_sensitivity(const float value) {
    m_config.set_camera_look_sensitivity(value);
}

void editor::editor_system::set_camera_zoom_sensitivity(const float value) {
    m_config.set_camera_zoom_sensitivity(value);
}

void editor::editor_system::set_ui_scale(const float value) {
    m_config.set_ui_scale(value);
}

void editor::editor_system::set_camera_min_z_far(const float value) {
    m_camera_min_z_far = value;
}

void editor::editor_system::set_grid_enabled(const bool value) {
    m_grid_gizmo.enabled = value;
}

void editor::editor_system::set_grid_spacing(const float value) {
    m_grid_gizmo.spacing = value;
}

void editor::editor_system::set_grid_extent(const float value) {
    m_grid_gizmo.extent = value;
}

void editor::editor_system::setup(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    m_registry = registry;
    m_config.initialize_interface();

    auto& root = bgui::set_layout<bgui::linear>(bgui::orientation::vertical);
    auto& menu_bar = root.add_persistent<bgui::menu_bar>(root);
    auto& project_menu = menu_bar.add_button("Projeto");
    project_menu.add_button("Novo projeto...", [this]() {
        const auto directory = choose_project_directory();
        if (!directory.empty())
            create_project(directory);
    });
    project_menu.add_button("Abrir projeto...", [this]() {
        const auto path = choose_file(false, false, true);
        if (!path.empty())
            open_project(path);
    });
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = registry;
    project_menu.add_button("Salvar projeto", [this, weak_registry]() {
        save_project(weak_registry.lock());
    });

    auto& scene_menu = menu_bar.add_button("Cena");
    scene_menu.add_button("Salvar cena...", [this, weak_registry]() {
        open_scene_file_dialog(true, weak_registry.lock());
    });
    scene_menu.add_button("Importar cena...", [this, weak_registry]() {
        open_scene_file_dialog(false, weak_registry.lock());
    });

    auto& config = menu_bar.add_button("Configurações");
    config.add_button("Câmera do editor", [this]() {
        m_ui.open_editor_camera_settings(*this);
    });

    auto& dock = root.add_persistent<bgui::dock>();
    dock.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::stretch);
    auto& scene_window = dock.add_window("Editor View Window", bgui::dock_area::center);
    auto& entities_window = dock.add_window("Entities", bgui::dock_area::left);
    auto& components_window = dock.add_window("Components", bgui::dock_area::right);
    auto& assets_window = dock.add_window("Assets Window", bgui::dock_area::right);
    m_config.load_interface();

    config.add_button("Restaurar interface padrão", [this, dock_ptr = &dock]() {
        bgui::dock::configuration default_configuration;
        default_configuration.windows = {
            {"Editor View Window", bgui::dock_area::center, 1.f},
            {"Entities", bgui::dock_area::left, 1.f},
            {"Components", bgui::dock_area::right, 0.5f},
            {"Assets Window", bgui::dock_area::right, 0.5f}
        };
        dock_ptr->apply_configuration(default_configuration);
        m_config.save_interface();
    });

    if (registry) {
        auto editor_camera_entity = registry->create();
        m_editor_camera_entity = editor_camera_entity.id;
        registry->add<COMMONS_NS::camera>(editor_camera_entity);
        registry->add<COMMONS_NS::directional_light>(
            editor_camera_entity,
            COMMONS_NS::fvec3{-0.2f, -1.f, -0.3f},
            COMMONS_NS::fvec3(0.15f),
            COMMONS_NS::fvec3(1.f),
            1.f
        );
        if (auto camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity))
            camera_transform->set_rotation(COMMONS_NS::fvec3{0.f, 90.f, 0.f});
        m_camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
        if (auto camera = m_camera.lock())
            camera->createFB();
    }

    setup_scene_view_panel(scene_window);
    setup_entities_panel(entities_window);
    setup_components_panel(components_window);
    setup_assets_panel(assets_window, registry);
    refresh_scene(registry);
    const auto recent_project = load_recent_project();
    if (!recent_project.empty()) {
        debugging::emit(info, "editor", "Carregando o projeto recente: " + recent_project);
        open_project(recent_project);
    } else {
        debugging::emit(info, "editor", "Nenhum projeto recente foi encontrado.");
    }
    bgui::cascade_style();
    bgui::load_font_queue();
}

void editor::editor_system::save_project(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry || m_project_config_path.empty()) {
        ui_elements::set_text(ui_elements::project_status, "Crie ou abra um projeto antes de salvar.");
        return;
    }

    const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
    const auto scenes_root = project_root / "Scenes";
    std::filesystem::path scene_path = scenes_root / "scene.bscene";
    if (!m_current_scene.empty()) {
        const auto current = std::filesystem::absolute(m_current_scene).lexically_normal();
        const auto absolute_scenes = std::filesystem::absolute(scenes_root).lexically_normal();
        const auto relative = current.lexically_relative(absolute_scenes);
        if (!relative.empty() && *relative.begin() != "..")
            scene_path = scenes_root / relative;
        else
            scene_path = scenes_root / current.filename();
    }

    std::error_code error;
    std::filesystem::create_directories(scene_path.parent_path(), error);
    if (error) {
        ui_elements::set_text(ui_elements::project_status, "Não foi possível criar a pasta de cenas do projeto.");
        return;
    }

    std::vector<std::string> scenes_in_project;
    scenes_in_project.reserve(m_project_scenes.size() + 1);
    for (const auto& scene : m_project_scenes) {
        auto source_path = std::filesystem::path(scene);
        if (source_path.is_relative())
            source_path = project_root / source_path;
        source_path = std::filesystem::absolute(source_path).lexically_normal();

        if (!project_assets::package_scene_assets(source_path, project_root)) {
            ui_elements::set_text(ui_elements::project_status, "Não foi possível empacotar os assets de uma cena do projeto.");
            return;
        }

        const auto relative_to_scenes = source_path.lexically_relative(
            std::filesystem::absolute(scenes_root).lexically_normal());
        auto destination_path = scenes_root / scene;
        if (!relative_to_scenes.empty() && *relative_to_scenes.begin() != "..") {
            destination_path = scenes_root / relative_to_scenes;
        } else {
            const auto relative_to_project = source_path.lexically_relative(
                std::filesystem::absolute(project_root).lexically_normal());
            destination_path = relative_to_project.empty() || *relative_to_project.begin() == ".."
                ? scenes_root / source_path.filename()
                : scenes_root / relative_to_project;
        }

        if (source_path != std::filesystem::absolute(destination_path).lexically_normal()) {
            std::filesystem::create_directories(destination_path.parent_path(), error);
            if (error) {
                ui_elements::set_text(ui_elements::project_status, "Não foi possível criar a pasta de uma cena do projeto.");
                return;
            }
            std::filesystem::copy_file(
                source_path, destination_path, std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                ui_elements::set_text(ui_elements::project_status, "Não foi possível copiar uma cena para a pasta Scenes.");
                return;
            }
        }
        scenes_in_project.push_back(destination_path.lexically_relative(project_root).generic_string());
    }

    if (!save_scene_file(scene_path.string(), registry)) {
        ui_elements::set_text(ui_elements::project_status, "Não foi possível salvar a cena atual do projeto.");
        return;
    }
    m_current_scene = std::filesystem::absolute(scene_path).lexically_normal().string();
    const auto relative_scene = scene_path.lexically_relative(project_root).generic_string();
    if (std::find(scenes_in_project.begin(), scenes_in_project.end(), relative_scene) == scenes_in_project.end())
        scenes_in_project.push_back(relative_scene);
    m_project_scenes = std::move(scenes_in_project);

    if (!save_editor_cache(registry)) {
        ui_elements::set_text(ui_elements::project_status, "Cenas salvas, mas não foi possível salvar o cache do editor.");
        return;
    }
    for (const auto& scene : m_project_scenes) {
        if (!project_assets::package_scene_assets(project_root / scene, project_root)) {
            ui_elements::set_text(ui_elements::project_status, "Não foi possível empacotar todos os assets do projeto.");
            return;
        }
    }
    if (!save_project_config()) {
        ui_elements::set_text(ui_elements::project_status, "Cenas e assets salvos, mas não foi possível atualizar o projeto.");
        return;
    }

    refresh_project_scenes();
    ui_elements::set_text(ui_elements::project_status, "Projeto salvo: " + m_project_name);
}

void editor::editor_system::update(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry)
        return;
    refresh_scene(registry);
    if (!ui_elements::contains(ui_elements::framebuffer_image))
        return;

    if (m_camera.expired()) {
        registry->cada<COMMONS_NS::camera>([&](const uint32_t entity) {
            if (entity == m_editor_camera_entity || !m_camera.expired())
                return;
            if (auto camera = registry->get<COMMONS_NS::camera>(entity))
                m_camera = camera;
        });
    }

    update_scene_view_panel(registry);

    const float current_time = bgui::get_time();
    if (!m_project_config_path.empty() &&
        current_time - m_last_editor_cache_save_time >= 1.f &&
        !save_editor_cache(registry))
        ui_elements::set_text(ui_elements::project_status, "Não foi possível salvar o cache do editor.");
}

void editor::editor_system::refresh_scene(const std::shared_ptr<COMMONS_NS::ecs>& registry) {
    if (!registry || !ui_elements::contains(ui_elements::entities_list) ||
        !ui_elements::contains(ui_elements::components_list))
        return;

    std::vector<std::pair<uint32_t, uint32_t>> signature;
    signature.reserve(registry->entities.size());
    for (const auto& entity_entry : registry->entities) {
        const auto entity_id = entity_entry.first;
        if (entity_id == m_editor_camera_entity)
            continue;
        signature.emplace_back(entity_id, static_cast<uint32_t>(registry->get_components(entity_id)));
    }
    if (m_scene_initialized && signature == m_scene_signature)
        return;

    m_scene_signature = std::move(signature);
    m_scene_initialized = true;
    const bool selected_exists = std::any_of(
        m_scene_signature.begin(), m_scene_signature.end(),
        [this](const auto& entry) { return entry.first == m_selected_entity; }
    );
    if (!selected_exists)
        m_selected_entity = m_scene_signature.empty() ? 0 : m_scene_signature.front().first;

    rebuild_entities(registry);
    rebuild_components(registry);
}

void editor::editor_system::open_scene_file_dialog(
    const bool save,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry)
        return;
    if (!ui_elements::contains(ui_elements::scene_file_dialog))
        create_scene_file_dialog();

    auto& scene_file_dialog = ui_elements::require<bgui::window>(ui_elements::scene_file_dialog);
    auto& scene_file_input = ui_elements::require<bgui::input_area>(ui_elements::scene_file_input);
    m_scene_file_save = save;
    m_scene_file_registry = registry;
    scene_file_dialog.set_title(save ? "Salvar .bscene" : "Importar .bscene");
    std::string initial_path;
    if (save) {
        if (!m_current_scene.empty()) {
            initial_path = m_current_scene;
        } else if (!m_project_config_path.empty()) {
            initial_path = (std::filesystem::path(m_project_config_path).parent_path() / "Scenes" / "scene.bscene").string();
        } else {
            initial_path = "scene.bscene";
        }
    }
    scene_file_input.set_buffer(initial_path);
    ui_elements::set_text(ui_elements::scene_file_status, "Informe o caminho do arquivo .bscene.");
    const auto size = bgui::get_context_size();
    scene_file_dialog.set_position(
        std::max(0, (size.x - scene_file_dialog.processed_width()) / 2),
        std::max(0, (size.y - scene_file_dialog.processed_height()) / 2)
    );
    scene_file_dialog.set_enable(true);
    scene_file_dialog.set_flex(false);
}

void editor::editor_system::create_scene_file_dialog() {
    auto& dialog = bgui::get_layout().add_persistent<bgui::window, bgui::layer::overlay>("Arquivo de cena");
    dialog.add_class(ui_elements::scene_file_dialog);
    dialog.style.layout.require_mode(bgui::mode::pixel, bgui::mode::pixel);
    dialog.style.layout.require_size(460.f, 190.f);

    auto& input = dialog.add_persistent<bgui::input_area>("", 0.35f, [this](const std::string) {
        apply_scene_file_path(ui_elements::require<bgui::input_area>(ui_elements::scene_file_input).get_buffer());
    }, "Caminho do arquivo .bscene");
    input.add_class(ui_elements::scene_file_input);
    input.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);

    auto& browse = dialog.add_persistent<bgui::button>("Procurar...", 0.35f, [this]() {
        const auto path = choose_file(m_scene_file_save, false);
        if (!path.empty())
            ui_elements::require<bgui::input_area>(ui_elements::scene_file_input).set_buffer(path);
    });
    browse.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);

    auto& status = dialog.add_persistent<bgui::text>("", 0.32f);
    status.add_class(ui_elements::scene_file_status);
    status.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);

    auto& actions = dialog.add_persistent<bgui::linear>(bgui::orientation::horizontal);
    actions.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    auto& confirm = actions.add_persistent<bgui::button>("Confirmar", 0.35f, [this]() {
        apply_scene_file_path(ui_elements::require<bgui::input_area>(ui_elements::scene_file_input).get_buffer());
    });
    confirm.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
    auto& cancel = actions.add_persistent<bgui::button>("Cancelar", 0.35f, [this]() {
        ui_elements::require<bgui::window>(ui_elements::scene_file_dialog).set_enable(false);
    });
    cancel.style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);

    dialog.set_enable(false);
}

void editor::editor_system::apply_scene_file_path(const std::string& path) {
    const auto registry = m_scene_file_registry.lock();
    if (!registry) {
        ui_elements::set_text(ui_elements::scene_file_status, "A cena não está mais disponível.");
        return;
    }
    if (path.empty()) {
        ui_elements::set_text(ui_elements::scene_file_status, "Informe um caminho válido.");
        return;
    }

    std::filesystem::path scene_path(path);
    if (scene_path.extension() != ".bscene") {
        if (!m_scene_file_save) {
            ui_elements::set_text(ui_elements::scene_file_status, "Selecione um arquivo com extensão .bscene.");
            return;
        }
        scene_path += ".bscene";
    }

    try {
        if (m_scene_file_save) {
            if (scene_path.is_relative() && !m_project_config_path.empty())
                scene_path = std::filesystem::path(m_project_config_path).parent_path() / scene_path;
            std::error_code error;
            if (!scene_path.parent_path().empty())
                std::filesystem::create_directories(scene_path.parent_path(), error);
            if (error) {
                ui_elements::set_text(ui_elements::scene_file_status, "Não foi possível criar a pasta da cena.");
                return;
            }
            if (!save_scene_file(scene_path.string(), registry)) {
                ui_elements::set_text(ui_elements::scene_file_status, "Não foi possível salvar o arquivo.");
                return;
            }
            if (!m_project_config_path.empty() &&
                !project_assets::package_scene_assets(
                    scene_path, std::filesystem::path(m_project_config_path).parent_path())) {
                ui_elements::set_text(ui_elements::scene_file_status, "Cena salva, mas não foi possível empacotar seus assets.");
                return;
            }
            m_current_scene = scene_path.lexically_normal().string();
            if (!m_project_config_path.empty()) {
                const auto project_root = std::filesystem::absolute(
                    std::filesystem::path(m_project_config_path).parent_path()).lexically_normal();
                const auto absolute_scene = std::filesystem::absolute(scene_path).lexically_normal();
                const auto relative_scene = absolute_scene.lexically_relative(project_root);
                if (!relative_scene.empty() && *relative_scene.begin() != "..") {
                    const auto relative = relative_scene.generic_string();
                    if (std::find(m_project_scenes.begin(), m_project_scenes.end(), relative) == m_project_scenes.end())
                        m_project_scenes.push_back(relative);
                    if (!save_project_config()) {
                        ui_elements::set_text(ui_elements::scene_file_status, "Cena salva, mas não foi possível atualizar o projeto.");
                        return;
                    }
                    refresh_project_scenes();
                }
            }
            ui_elements::set_text(ui_elements::scene_file_status, "Cena salva: " + scene_path.string());
        } else {
                if (m_project_config_path.empty()) {
                    ui_elements::set_text(ui_elements::scene_file_status, "Crie ou abra um projeto antes de importar cenas.");
                    return;
                }
                const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
                const auto destination = project_root / "Scenes" / scene_path.filename();
                std::error_code error;
                std::filesystem::create_directories(destination.parent_path(), error);
                if (error || !std::filesystem::is_regular_file(scene_path, error)) {
                    ui_elements::set_text(ui_elements::scene_file_status, "Não foi possível acessar a cena de origem.");
                    return;
                }
                std::filesystem::copy_file(scene_path, destination, std::filesystem::copy_options::overwrite_existing, error);
                if (error) {
                    ui_elements::set_text(ui_elements::scene_file_status, "Não foi possível copiar a cena para o projeto.");
                    return;
                }
                const auto relative_scene = destination.lexically_relative(project_root).generic_string();
                if (std::find(m_project_scenes.begin(), m_project_scenes.end(), relative_scene) == m_project_scenes.end())
                    m_project_scenes.push_back(relative_scene);
                refresh_project_scenes();
                if (!save_project_config()) {
                    ui_elements::set_text(ui_elements::scene_file_status, "Cena importada, mas não foi possível atualizar o projeto.");
                    return;
                }
                ui_elements::set_text(ui_elements::scene_file_status, "Cena adicionada ao projeto: " + scene_path.filename().string());
            }
    } catch (const std::exception& error) {
        ui_elements::set_text(ui_elements::scene_file_status, std::string("Erro: ") + error.what());
    }
}

void editor::editor_system::create_project(const std::string& directory) {
    if (directory.empty())
        return;

    const auto project_root = std::filesystem::path(directory);
    std::error_code error;
    std::filesystem::create_directories(project_root / "Assets", error);
    if (error) {
        ui_elements::set_text(ui_elements::project_status, "Não foi possível criar a pasta do projeto.");
        return;
    }
    std::filesystem::create_directories(project_root / "Scenes", error);
    if (error) {
        ui_elements::set_text(ui_elements::project_status, "Não foi possível criar as pastas do projeto.");
        return;
    }
    m_project_name = project_root.filename().string();
    m_project_config_path = (project_root / "project.bproject").string();
    m_project_scenes.clear();

    if (!save_editor_cache(m_registry.lock())) {
        ui_elements::set_text(ui_elements::project_status, "Projeto criado, mas não foi possível criar o cache do editor.");
        return;
    }

    ui_elements::set_text(ui_elements::project_status, "Projeto aberto: " + m_project_name);

    if (!save_project_config())
        ui_elements::set_text(ui_elements::project_status, "Não foi possível salvar a configuração do projeto.");
    else if (!remember_recent_project(m_project_config_path))
        debugging::emit(alerta, "editor", "Não foi possível registrar o projeto recente.");
    refresh_project_scenes();
}

void editor::editor_system::open_project(const std::string& path) {
    if (path.empty())
        return;

    debugging::emit(info, "editor", "Abrindo projeto: " + path);
    const auto project_path = std::filesystem::path(path);
    std::ifstream input(project_path, std::ios::binary);
    if (!input) {
        debugging::emit(erro, "editor", "Não foi possível abrir o arquivo do projeto: " + path);
        ui_elements::set_text(ui_elements::project_status, "Não foi possível abrir o arquivo do projeto.");
        return;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError() || !document.IsObject()) {
        debugging::emit(erro, "editor", "Arquivo de projeto inválido: " + path);
        ui_elements::set_text(ui_elements::project_status, "Arquivo de projeto inválido.");
        return;
    }

    std::string project_name = project_path.parent_path().filename().string();
    std::vector<std::string> project_scenes;
    const auto name = document.FindMember("name");
    if (name != document.MemberEnd()) {
        if (!name->value.IsString()) {
            debugging::emit(erro, "editor", "Nome inválido na configuração do projeto: " + path);
            ui_elements::set_text(ui_elements::project_status, "Nome inválido na configuração do projeto.");
            return;
        }
        project_name = name->value.GetString();
    }
    const auto scenes = document.FindMember("scenes");
    if (scenes != document.MemberEnd()) {
        if (!scenes->value.IsArray()) {
            debugging::emit(erro, "editor", "Lista de cenas inválida no projeto: " + path);
            ui_elements::set_text(ui_elements::project_status, "Lista de cenas inválida no projeto.");
            return;
        }
        for (const auto& scene : scenes->value.GetArray()) {
            if (scene.IsString())
                project_scenes.emplace_back(scene.GetString());
        }
    }

    m_project_config_path = project_path.string();
    m_project_name = std::move(project_name);
    m_project_scenes = std::move(project_scenes);

    if (m_project_scenes.empty()) {
        const auto project_root = project_path.parent_path();
        const auto scenes_dir = project_root / "Scenes";
        std::error_code error;
        if (std::filesystem::exists(scenes_dir, error)) {
            for (const auto& entry : std::filesystem::directory_iterator(scenes_dir, error)) {
                if (entry.is_regular_file(error) && entry.path().extension() == ".bscene") {
                    const auto relative = entry.path().lexically_relative(project_root).generic_string();
                    m_project_scenes.push_back(relative);
                }
            }
        }
    }

    const auto registry = m_registry.lock();
    const bool cache_loaded = load_editor_cache(registry);
    if (!cache_loaded)
        debugging::emit(alerta, "editor", "Projeto aberto sem carregar o cache do editor.");
    if (!remember_recent_project(project_path))
        debugging::emit(alerta, "editor", "Não foi possível registrar o projeto recente.");
    debugging::emit(info, "editor", "Projeto carregado: " + m_project_name);
    ui_elements::set_text(ui_elements::project_status, "Projeto aberto: " + m_project_name);

    refresh_project_scenes();
    if (!cache_loaded)
        ui_elements::set_text(ui_elements::project_status, "Projeto aberto, mas não foi possível carregar o cache do editor.");
}

bool editor::editor_system::save_project_config() {
    if (m_project_config_path.empty())
        return false;

    const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
    std::error_code error;
    std::filesystem::create_directories(project_root, error);

    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();
    document.AddMember("name", rapidjson::Value(m_project_name.c_str(), static_cast<rapidjson::SizeType>(m_project_name.size()), allocator), allocator);

    rapidjson::Value scenes(rapidjson::kArrayType);
    for (const auto& scene : m_project_scenes) {
        const auto normalized = std::filesystem::path(scene).generic_string();
        scenes.PushBack(rapidjson::Value(normalized.c_str(), static_cast<rapidjson::SizeType>(normalized.size()), allocator), allocator);
    }
    document.AddMember("scenes", scenes, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    if (error)
    return false;
    return write_file_atomically(
    m_project_config_path,
    std::string(buffer.GetString(), buffer.GetSize()));
}

bool editor::editor_system::save_editor_cache(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || m_project_config_path.empty() || m_editor_camera_entity == 0)
        return false;

    const auto camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
    const auto camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity);
    const auto ambient_light = registry->get<COMMONS_NS::directional_light>(m_editor_camera_entity);
    if (!camera || !camera_transform || !ambient_light)
        return false;

    const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
    const auto cache_path = project_root / "Cache" / "editor_scene.json";
    std::error_code error;
    std::filesystem::create_directories(cache_path.parent_path(), error);
    if (error)
        return false;

    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();
    document.AddMember("format", rapidjson::Value("cpp-bengine-editor-cache", allocator), allocator);
    document.AddMember("version", 2, allocator);

    rapidjson::Value editor_camera(rapidjson::kObjectType);
    rapidjson::Value transform(rapidjson::kObjectType);
    camera_transform->serialize(transform, allocator);
    editor_camera.AddMember("transform", transform, allocator);

    rapidjson::Value camera_settings(rapidjson::kObjectType);
    camera_settings.AddMember("fov", camera->fov, allocator);
    camera_settings.AddMember("near_clip", camera->corte_curto, allocator);
    camera_settings.AddMember("far_clip", camera->corte_longo, allocator);
    camera_settings.AddMember("scale", camera->scale, allocator);
    camera_settings.AddMember("orthographic", camera->flag_orth, allocator);
    rapidjson::Value background(rapidjson::kArrayType);
    background.PushBack(camera->ceu.r, allocator);
    background.PushBack(camera->ceu.g, allocator);
    background.PushBack(camera->ceu.b, allocator);
    background.PushBack(camera->ceu.a, allocator);
    camera_settings.AddMember("background", background, allocator);
    editor_camera.AddMember("camera", camera_settings, allocator);

    rapidjson::Value serialized_light(rapidjson::kObjectType);
    if (!ambient_light->serialize(serialized_light, allocator))
        return false;
    editor_camera.AddMember("ambient_light", serialized_light, allocator);
    document.AddMember("editor_camera", editor_camera, allocator);

    rapidjson::Value grid(rapidjson::kObjectType);
    grid.AddMember("enabled", m_grid_gizmo.enabled, allocator);
    grid.AddMember("spacing", m_grid_gizmo.spacing, allocator);
    grid.AddMember("extent", m_grid_gizmo.extent, allocator);
    document.AddMember("grid", grid, allocator);

    rapidjson::Value camera_controls(rapidjson::kObjectType);
    camera_controls.AddMember("move_speed", m_config.camera_move_speed(), allocator);
    camera_controls.AddMember("look_sensitivity", m_config.camera_look_sensitivity(), allocator);
    camera_controls.AddMember("zoom_sensitivity", m_config.camera_zoom_sensitivity(), allocator);
    document.AddMember("camera_controls", camera_controls, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);

    if (!write_file_atomically(cache_path, std::string(buffer.GetString(), buffer.GetSize())))
        return false;
    m_last_editor_cache_save_time = bgui::get_time();
    return true;
}

bool editor::editor_system::load_editor_cache(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || m_project_config_path.empty() || m_editor_camera_entity == 0)
        return false;

    const auto camera = registry->get<COMMONS_NS::camera>(m_editor_camera_entity);
    const auto camera_transform = registry->get<COMMONS_NS::transform>(m_editor_camera_entity);
    const auto ambient_light = registry->get<COMMONS_NS::directional_light>(m_editor_camera_entity);
    if (!camera || !camera_transform || !ambient_light)
        return false;

    camera_transform->set_position(COMMONS_NS::fvec3{0.f});
    camera_transform->set_rotation(COMMONS_NS::fvec3{0.f, 90.f, 0.f});
    camera_transform->set_scale(COMMONS_NS::fvec3(1.f));
    camera->fov = 75.f;
    camera->corte_curto = 0.1f;
    camera->corte_longo = 300.f;
    camera->scale = 5.f;
    camera->flag_orth = false;
    camera->ceu = {0.43f, 0.78f, 0.86f, 1.f};
    ambient_light->direction = {-0.2f, -1.f, -0.3f};
    ambient_light->ambient = COMMONS_NS::fvec3(0.15f);
    ambient_light->color = COMMONS_NS::fvec3(1.f);
    ambient_light->intensity = 1.f;
    m_grid_gizmo.enabled = true;
    m_grid_gizmo.spacing = 1.f;
    m_grid_gizmo.extent = 50.f;
    m_config.set_camera_move_speed(4.f);
    m_config.set_camera_look_sensitivity(0.12f);
    m_config.set_camera_zoom_sensitivity(3.f);

    const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
    const auto cache_path = project_root / "Cache" / "editor_scene.json";
    std::error_code error;
    if (!std::filesystem::exists(cache_path, error)) {
        if (error)
            return false;
        return save_editor_cache(registry);
    }

    std::ifstream input(cache_path, std::ios::binary);
    if (!input)
        return false;
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError() || !document.IsObject())
        return false;

    const auto format = document.FindMember("format");
    const auto version = document.FindMember("version");
    if (format == document.MemberEnd() || !format->value.IsString() ||
        std::string(format->value.GetString()) != "cpp-bengine-editor-cache" ||
        version == document.MemberEnd() || !version->value.IsInt() ||
        (version->value.GetInt() != 1 && version->value.GetInt() != 2))
        return false;
    const int cache_version = version->value.GetInt();

    const auto* editor_camera = member_value(document, "editor_camera");
    const auto* transform = editor_camera ? member_value(*editor_camera, "transform") : nullptr;
    const auto* camera_settings = editor_camera ? member_value(*editor_camera, "camera") : nullptr;
    const auto* light = editor_camera ? member_value(*editor_camera, "ambient_light") : nullptr;
    const auto* grid = member_value(document, "grid");
    const auto* controls = member_value(document, "camera_controls");
    if (!transform || !camera_settings || !light || !grid || !controls)
        return false;

    float position_values[3]{};
    float rotation_values[3]{};
    float scale_values[3]{};
    if (!read_float_array(*transform, "position", position_values, 3) ||
        !read_float_array(*transform, "rotation", rotation_values, 3) ||
        !read_float_array(*transform, "scale", scale_values, 3))
        return false;

    float fov = 0.f;
    float near_clip = 0.f;
    float far_clip = 0.f;
    float camera_scale = 0.f;
    float background_values[4]{};
    const auto* orthographic = member_value(*camera_settings, "orthographic");
    if (!read_float(*camera_settings, "fov", fov) ||
        !read_float(*camera_settings, "near_clip", near_clip) ||
        !read_float(*camera_settings, "far_clip", far_clip) ||
        !read_float(*camera_settings, "scale", camera_scale) ||
        !read_float_array(*camera_settings, "background", background_values, 4) ||
        !orthographic || !orthographic->IsBool() ||
        fov <= 0.f || fov >= 180.f || near_clip <= 0.f ||
        far_clip <= near_clip || camera_scale <= 0.f)
        return false;

    float light_direction[3]{};
    float light_ambient[3]{};
    float light_color[3]{};
    float light_intensity = 0.f;
    if (!read_float_array(*light, "direction", light_direction, 3) ||
        !read_float_array(*light, "ambient", light_ambient, 3) ||
        !read_float_array(*light, "color", light_color, 3) ||
        !read_float(*light, "intensity", light_intensity))
        return false;
    if (cache_version == 1 &&
        std::abs(light_ambient[0] - 0.15f) < 0.0001f &&
        std::abs(light_ambient[1]) < 0.0001f &&
        std::abs(light_ambient[2]) < 0.0001f &&
        std::abs(light_color[0] - 1.f) < 0.0001f &&
        std::abs(light_color[1]) < 0.0001f &&
        std::abs(light_color[2]) < 0.0001f) {
        light_ambient[1] = light_ambient[2] = light_ambient[0];
        light_color[1] = light_color[2] = light_color[0];
    }
    if (cache_version == 1 &&
        std::abs(scale_values[0] - 1.f) < 0.0001f &&
        std::abs(scale_values[1]) < 0.0001f &&
        std::abs(scale_values[2]) < 0.0001f) {
        scale_values[1] = scale_values[2] = scale_values[0];
    }

    const auto* grid_enabled = member_value(*grid, "enabled");
    float grid_spacing = 0.f;
    float grid_extent = 0.f;
    if (!grid_enabled || !grid_enabled->IsBool() ||
        !read_float(*grid, "spacing", grid_spacing) ||
        !read_float(*grid, "extent", grid_extent) ||
        grid_spacing <= 0.f || grid_extent <= 0.f)
        return false;

    float move_speed = 0.f;
    float look_sensitivity = 0.f;
    float zoom_sensitivity = 0.f;
    if (!read_float(*controls, "move_speed", move_speed) ||
        !read_float(*controls, "look_sensitivity", look_sensitivity) ||
        !read_float(*controls, "zoom_sensitivity", zoom_sensitivity) ||
        move_speed <= 0.f || look_sensitivity <= 0.f || zoom_sensitivity <= 0.f)
        return false;

    camera_transform->set_position(COMMONS_NS::fvec3{position_values[0], position_values[1], position_values[2]});
    camera_transform->set_rotation(COMMONS_NS::fvec3{rotation_values[0], rotation_values[1], rotation_values[2]});
    camera_transform->set_scale(COMMONS_NS::fvec3{scale_values[0], scale_values[1], scale_values[2]});
    camera->fov = fov;
    camera->corte_curto = near_clip;
    camera->corte_longo = far_clip;
    camera->scale = camera_scale;
    camera->flag_orth = orthographic->GetBool();
    camera->ceu = {background_values[0], background_values[1], background_values[2], background_values[3]};
    ambient_light->direction = {light_direction[0], light_direction[1], light_direction[2]};
    ambient_light->ambient = {light_ambient[0], light_ambient[1], light_ambient[2]};
    ambient_light->color = {light_color[0], light_color[1], light_color[2]};
    ambient_light->intensity = light_intensity;
    m_grid_gizmo.enabled = grid_enabled->GetBool();
    m_grid_gizmo.spacing = grid_spacing;
    m_grid_gizmo.extent = grid_extent;
    m_config.set_camera_move_speed(move_speed);
    m_config.set_camera_look_sensitivity(look_sensitivity);
    m_config.set_camera_zoom_sensitivity(zoom_sensitivity);
    m_scene_initialized = false;
    m_last_editor_cache_save_time = bgui::get_time();
    if (cache_version == 1 && !save_editor_cache(registry))
        debugging::emit(alerta, "editor", "Cache antigo carregado, mas não foi possível atualizá-lo.");
    return true;
}

void editor::editor_system::refresh_project_scenes() {
    if (m_project_config_path.empty())
        ui_elements::set_text(ui_elements::project_status, "Nenhum projeto aberto");
    else
        ui_elements::set_text(ui_elements::project_status, "Projeto: " + m_project_name);

    if (!ui_elements::contains(ui_elements::project_scenes_list))
        return;

    auto& project_scenes_list = ui_elements::require<bgui::linear>(ui_elements::project_scenes_list);
    for (auto& layer_elements : project_scenes_list.get_elements())
        layer_elements.second.clear();

    std::vector<std::string> scenes = m_project_scenes;
    std::sort(scenes.begin(), scenes.end());
    const std::weak_ptr<COMMONS_NS::ecs> weak_registry = m_registry;

    for (const auto& scene : scenes) {
        auto& item = project_scenes_list.add_persistent<bgui::button>(scene, 0.35f, [this, scene, weak_registry]() {
            load_project_scene(scene, weak_registry.lock());
        });
        item.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
    }
}

void editor::editor_system::load_project_scene(
    const std::string& scene,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || m_project_config_path.empty())
        return;

    auto scene_path = std::filesystem::path(scene);
    if (scene_path.is_relative())
        scene_path = std::filesystem::path(m_project_config_path).parent_path() / scene_path;
    std::error_code error;
    if (!std::filesystem::is_regular_file(scene_path, error)) {
        ui_elements::set_text(ui_elements::project_status, "Cena não encontrada: " + scene);
        return;
    }

    std::unordered_set<uint32_t> previous_entities;
    for (const auto& entry : registry->entities)
        previous_entities.insert(entry.first);

    const auto rollback_new_entities = [&]() {
        std::vector<uint32_t> new_entities;
        for (const auto& entry : registry->entities) {
            if (!previous_entities.contains(entry.first))
                new_entities.push_back(entry.first);
        }
        for (const auto entity_id : new_entities)
            registry->remove(entity_id);
    };

    std::size_t imported = 0;
    try {
        imported = import_scene_file(scene_path.string(), registry);
    } catch (const std::exception& exception) {
        rollback_new_entities();
        ui_elements::set_text(ui_elements::project_status, std::string("Falha ao carregar cena: ") + exception.what());
        return;
    }

    if (imported == 0) {
        std::ifstream input(scene_path, std::ios::binary);
        const std::string contents((std::istreambuf_iterator<char>(input)), {});
        rapidjson::Document document;
        document.Parse(contents.c_str());
        if (document.HasParseError()) {
            rollback_new_entities();
            ui_elements::set_text(ui_elements::project_status, "Arquivo de cena inválido; a cena atual foi mantida.");
            return;
        }
        const auto entities = document.IsObject() ? document.FindMember("entities") : document.MemberEnd();
        if (!document.IsObject() || (entities != document.MemberEnd() && entities->value.IsArray() && !entities->value.Empty())) {
            rollback_new_entities();
            ui_elements::set_text(ui_elements::project_status, "Nenhum modelo válido foi carregado; a cena atual foi mantida.");
            return;
        }
    }

    uint32_t first_loaded_entity = 0;
    for (const auto& entry : registry->entities) {
        if (previous_entities.contains(entry.first))
            continue;
        if (first_loaded_entity == 0)
            first_loaded_entity = entry.first;
    }
    for (const auto entity_id : previous_entities) {
        if (entity_id != m_editor_camera_entity)
            registry->remove(entity_id);
    }

    m_current_scene = scene_path.lexically_normal().string();
    m_selected_entity = first_loaded_entity;
    m_scene_initialized = false;
    refresh_scene(registry);
    ui_elements::set_text(ui_elements::project_status, "Cena carregada: " + scene);
}

void editor::editor_system::import_model_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    if (!registry || path.empty())
        return;

    try {
        auto entity = registry->create();
        registry->add<COMMONS_NS::renderer>(entity, path.c_str());
        const auto renderer = registry->get<COMMONS_NS::renderer>(entity.id);
        if (!renderer || !renderer->m_modelo || renderer->m_modelo->meshes.empty()) {
            registry->remove(entity.id);
            ui_elements::set_text(ui_elements::model_import_status, "Não foi possível carregar esse modelo.");
            return;
        }

        m_selected_entity = entity.id;
        m_scene_initialized = false;
        refresh_scene(registry);
        ui_elements::set_text(
            ui_elements::model_import_status,
            "Modelo importado: " + std::filesystem::path(path).filename().string());
    } catch (const std::exception& error) {
        ui_elements::set_text(ui_elements::model_import_status, std::string("Erro: ") + error.what());
    }
}

void editor::editor_system::browse_model_file(
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    const auto path = choose_file(false, true);
    if (!path.empty())
        import_model_file(path, registry);
}

bool editor::editor_system::save_scene_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    rapidjson::Document document;
    document.SetObject();
    auto& allocator = document.GetAllocator();
    document.AddMember("format", rapidjson::Value("cpp-bengine-scene", allocator), allocator);
    document.AddMember("version", 1, allocator);
    rapidjson::Value entities(rapidjson::kArrayType);

    for (const auto& [entity_id, components] : registry->entities) {
        if (entity_id == m_editor_camera_entity)
            continue;
        const auto render_component = registry->get<COMMONS_NS::renderer>(entity_id);
        const auto transform_component = registry->get<COMMONS_NS::transform>(entity_id);
        if (!render_component || !render_component->m_modelo || !transform_component)
            continue;

        rapidjson::Value entity(rapidjson::kObjectType);
        rapidjson::Value serialized_components(rapidjson::kObjectType);
        rapidjson::Value serialized_transform(rapidjson::kObjectType);
        rapidjson::Value serialized_renderer(rapidjson::kObjectType);
        transform_component->serialize(serialized_transform, allocator);
        if (!render_component->serialize(serialized_renderer, allocator))
            continue;
        const auto model_member = serialized_renderer.FindMember("model");
        if (model_member != serialized_renderer.MemberEnd()) {
            const std::filesystem::path source_path(render_component->m_modelo->get_source_path());
            std::string serialized_path;
            if (!m_project_config_path.empty()) {
                const auto project_root = std::filesystem::path(m_project_config_path).parent_path();
                std::error_code error;
                const auto absolute_source = std::filesystem::absolute(source_path, error);
                if (error)
                    return false;
                const auto absolute_root = std::filesystem::absolute(project_root, error);
                if (error)
                    return false;
                const auto relative_existing = absolute_source.lexically_relative(absolute_root);
                serialized_path = relative_existing.empty() || *relative_existing.begin() == ".."
                    ? absolute_source.generic_string()
                    : relative_existing.generic_string();
            } else {
                serialized_path = relative_asset_path(source_path);
            }
            model_member->value.SetString(serialized_path.c_str(), static_cast<rapidjson::SizeType>(serialized_path.size()), allocator);
        }
        serialized_components.AddMember("transform", serialized_transform, allocator);
        serialized_components.AddMember("renderer", serialized_renderer, allocator);
        entity.AddMember("components", serialized_components, allocator);
        entities.PushBack(entity, allocator);
    }
    document.AddMember("entities", entities, allocator);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    return write_file_atomically(path, std::string(buffer.GetString(), buffer.GetSize()));
}

std::size_t editor::editor_system::import_scene_file(
    const std::string& path,
    const std::shared_ptr<COMMONS_NS::ecs>& registry)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("não foi possível abrir " + path);
    const std::string contents(std::istreambuf_iterator<char>(input), {});
    rapidjson::Document document;
    document.Parse(contents.c_str());
    if (document.HasParseError())
        throw std::runtime_error("o arquivo não contém JSON válido");

    std::size_t imported = 0;
    const auto project_assets = m_project_config_path.empty()
        ? std::filesystem::path{}
        : std::filesystem::path(m_project_config_path).parent_path() / "Assets";
    import_models(document, nullptr, false, std::filesystem::path(path).parent_path(), project_assets, registry, imported);
    return imported;
}