#pragma once

#include <rapidjson/document.h>
#include <resources/executable_archive.hpp>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <string>

namespace game_package {
namespace fs = std::filesystem;

inline bool within(const fs::path& path, const fs::path& root) {
    const auto relative = path.lexically_relative(root);
    return !relative.empty() && *relative.begin() != ".." && !relative.is_absolute();
}
inline rapidjson::Document read_json(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open: " + path.string());
    const std::string contents{std::istreambuf_iterator<char>(input), {}};
    rapidjson::Document document;
    document.Parse(contents.data(), contents.size());
    if (input.bad() || document.HasParseError() || !document.IsObject())
        throw std::runtime_error("Invalid game JSON: " + path.string());
    return document;
}
inline void write_json(const fs::path& path, const rapidjson::Value& document) {
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(buffer.GetString(), buffer.GetSize());
    output.close();
    if (!output) throw std::runtime_error("Could not write: " + path.string());
}
inline bool game_manifest(const rapidjson::Value& document) {
    return document.HasMember("format") && document["format"].IsString() &&
        std::string(document["format"].GetString()) == "bubble-engine-game" &&
        document.HasMember("version") && document["version"].IsInt() && document["version"].GetInt() == 1;
}
inline void validate_destination(const fs::path& output) {
    if (!fs::exists(output) || (fs::is_directory(output) && fs::is_empty(output))) return;
    if (fs::is_regular_file(output / "game.json") && game_manifest(read_json(output / "game.json"))) return;
    if (fs::is_directory(output)) {
        auto it=fs::directory_iterator(output);
        const auto first=it->path();
        if (++it==fs::directory_iterator{} && fs::is_regular_file(first)) {
            const auto archive=executable_archive::reader::open(first);
            if(archive && archive->contains("game.json")) {
                const auto bytes=archive->read("game.json");rapidjson::Document manifest;
                manifest.Parse(reinterpret_cast<const char*>(bytes.data()),bytes.size());
                if(!manifest.HasParseError() && manifest.IsObject() && game_manifest(manifest))return;
            }
        }
    }
    throw std::runtime_error("Output is not an exported game; choose an empty directory: " + output.string());
}
inline bool excluded(const fs::path& relative) {
    for (const auto& part : relative) {
        const auto name = part.string();
        if (name == ".git" || name == ".github" || name == ".codex" || name == ".idea" || name == ".vscode" ||
            name == "build" || name == "out" || name == "CMakeFiles" || name == "platform-builds" ||
            name.find(".migration-backup-") != std::string::npos || name == "__pycache__" || name == "node_modules" || name.starts_with("cmake-build-")) return true;
    }
    const auto name = relative.filename().string();
    const bool asset_tree = *relative.begin() == "assets";
    const auto extension = relative.extension().string();
    if (!asset_tree && (extension == ".cpp" || extension == ".hpp" || extension == ".c" ||
        extension == ".h" || extension == ".md" || extension == ".sh" || extension == ".ps1" ||
        extension == ".yml" || extension == ".yaml" || name.starts_with('.'))) return true;
    return name == "editor-ui.cfg" || name == ".bengine-main-scene" ||
        name == "CMakeCache.txt" || name == "CMakeLists.txt" || name == "Makefile" ||
        name.ends_with(".preview.png") || name.ends_with(".log") || name.ends_with(".progress") ||
        name.ends_with(".pdb") || name.ends_with(".ilk") || name.ends_with(".o") ||
        name.ends_with(".a") || name.ends_with(".cmake") || name.ends_with(".sln") ||
        name.ends_with(".vcxproj") || name.ends_with(".vcxproj.filters");
}

class preparation {
    fs::path project_root, engine_root;
public:
    preparation(fs::path project, fs::path engine)
        : project_root(fs::canonical(project)), engine_root(fs::canonical(engine)) {}

    std::string asset(const std::string& value, bool builtin_model = false) const {
        if (value.empty()) return value;
        const fs::path source(value);
        auto resolved = fs::weakly_canonical(source.is_absolute() ? source : project_root / source);
        // Match the engine's existing fallback for bundled primitive models.
        if (!fs::is_regular_file(resolved) && builtin_model && fs::is_regular_file(engine_root / "models" / source.filename()))
            resolved = fs::canonical(engine_root / "models" / source.filename());
        if (!fs::is_regular_file(resolved)) throw std::runtime_error("Missing game asset: " + value);
        if (within(resolved, project_root)) {
            const auto relative = resolved.lexically_relative(project_root);
            if (excluded(relative)) throw std::runtime_error("Game references an excluded build/editor file: " + value);
            return relative.generic_string();
        }
        if (within(resolved, engine_root))
            return (fs::path("../assets") / resolved.lexically_relative(engine_root)).generic_string();
        throw std::runtime_error("Import this external asset into the project before exporting: " + value);
    }

    std::string dependency(const std::string& value, const fs::path& parent) const {
        auto normalized = value;
        std::replace(normalized.begin(), normalized.end(), '\\', '/');
        const fs::path reference(normalized);
        const auto source = fs::weakly_canonical(reference.is_absolute() ? reference : parent / reference);
        const auto mapped = asset(source.string());
        const auto packaged = (fs::path("game") / mapped).lexically_normal();
        return packaged.lexically_relative(fs::path("game") / parent.lexically_relative(project_root)).generic_string();
    }

    void model_document(const fs::path& source, const fs::path& target) const {
        auto extension = source.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".bmodel" || extension == ".bmat") {
            auto document = read_json(source);
            const auto rewrite_native = [&](auto&& self, rapidjson::Value& value) -> void {
                if (value.IsObject()) {
                    for (auto& member : value.GetObject()) {
                        const std::string_view key(member.name.GetString(), member.name.GetStringLength());
                        if ((key == "material" || key == "path") && member.value.IsString() && member.value.GetStringLength()) {
                            const auto mapped = dependency(member.value.GetString(), source.parent_path());
                            member.value.SetString(mapped.c_str(), static_cast<rapidjson::SizeType>(mapped.size()), document.GetAllocator());
                        } else self(self, member.value);
                    }
                } else if (value.IsArray()) for (auto& item : value.GetArray()) self(self, item);
            };
            rewrite_native(rewrite_native, document);
            write_json(target, document);
            return;
        }
        if (extension == ".gltf") {
            auto document = read_json(source);
            const auto rewrite_uri = [&](auto&& self, rapidjson::Value& value) -> void {
                if (value.IsObject()) {
                    for (auto& member : value.GetObject()) {
                        if (std::string(member.name.GetString()) == "uri" && member.value.IsString()) {
                            const std::string uri = member.value.GetString();
                            if (uri.starts_with("data:")) continue;
                            if (uri.find("://") != std::string::npos)
                                throw std::runtime_error("Import external glTF resources before exporting: " + uri);
                            std::string decoded;
                            for (std::size_t i = 0; i < uri.size(); ++i) {
                                if (uri[i] == '%' && i + 2 < uri.size() &&
                                    std::isxdigit(static_cast<unsigned char>(uri[i+1])) &&
                                    std::isxdigit(static_cast<unsigned char>(uri[i+2]))) {
                                    decoded += static_cast<char>(std::stoi(uri.substr(i+1, 2), nullptr, 16)); i += 2;
                                } else decoded += uri[i];
                            }
                            auto path = dependency(decoded, source.parent_path());
                            std::string encoded;
                            for (char c : path) encoded += c == ' ' ? "%20" : std::string(1, c);
                            member.value.SetString(encoded.c_str(), static_cast<rapidjson::SizeType>(encoded.size()), document.GetAllocator());
                        } else self(self, member.value);
                    }
                } else if (value.IsArray()) for (auto& item : value.GetArray()) self(self, item);
            };
            rewrite_uri(rewrite_uri, document);
            write_json(target, document);
            return;
        }
        std::ifstream input(source);
        std::ofstream output(target);
        for (std::string line; std::getline(input, line);) {
            std::istringstream parser(line);
            std::string command; parser >> command;
            const bool reference = extension == ".obj" ? command == "mtllib" :
                command.starts_with("map_") || command == "bump" || command == "norm" || command == "disp" || command == "decal";
            if (reference) {
                std::string rest; std::getline(parser >> std::ws, rest);
                std::istringstream words(rest);
                std::vector<std::string> tokens;
                for (std::string word; words >> std::quoted(word);) tokens.push_back(word);
                if (extension == ".obj" && tokens.size() > 1 &&
                    !fs::is_regular_file(source.parent_path() / rest)) {
                    output << command;
                    for (const auto& token : tokens)
                        output << ' ' << std::quoted(dependency(token, source.parent_path()));
                    output << '\n';
                    continue;
                }
                bool found = false;
                for (std::size_t i = 0; i < tokens.size(); ++i) {
                    std::string candidate;
                    for (std::size_t j = i; j < tokens.size(); ++j) candidate += (j == i ? "" : " ") + tokens[j];
                    std::replace(candidate.begin(), candidate.end(), '\\', '/');
                    if (!fs::is_regular_file(source.parent_path() / fs::path(candidate))) continue;
                    output << command;
                    for (std::size_t j = 0; j < i; ++j) output << ' ' << tokens[j];
                    output << ' ' << dependency(candidate, source.parent_path()) << '\n';
                    found = true; break;
                }
                if (!found) throw std::runtime_error("Missing or unsupported model dependency in " + source.string() + ": " + rest);
            } else output << line << '\n';
        }
        output.close();
        if (input.bad() || !output) throw std::runtime_error("Could not package model sidecar: " + source.string());
    }

    void rewrite(rapidjson::Value& value, rapidjson::Document::AllocatorType& allocator,
                 const std::string& role = "") const {
        if (value.IsString() && (role == "scene" || role == "model" || role == "directory" || role == "scripts" ||
                                role == "maps" || role == "file" || role == "skybox_faces" || role == "skybox_image" || role == "image")) {
            const auto path = asset(value.GetString(), role == "model");
            value.SetString(path.c_str(), static_cast<rapidjson::SizeType>(path.size()), allocator);
        } else if (value.IsArray()) {
            for (auto& item : value.GetArray()) rewrite(item, allocator, role);
        } else if (value.IsObject()) {
            const bool image = value.HasMember("type") && value["type"].IsString() &&
                std::string(value["type"].GetString()) == "image";
            for (auto& member : value.GetObject()) {
                const std::string key(member.name.GetString());
                rewrite(member.value, allocator, role == "maps" ? "maps" :
                    image && key == "value" ? "image" : key);
            }
        }
    }
};

inline void prepare(const fs::path& repository, const fs::path& project_file,
                    const fs::path& staging, const fs::path& output) {
    const auto project = fs::canonical(project_file);
    const auto root = project.parent_path();
    const auto destination = fs::weakly_canonical(output);
    const auto stage = fs::weakly_canonical(staging);
    if (within(root, destination) || within(destination, stage) || within(stage, destination) ||
        within(stage, root) || within(root, stage) || within(fs::canonical(repository), stage))
        throw std::runtime_error("Game output/cache must be separate from the source project and each other.");
    validate_destination(output);
    if (within(root, destination.parent_path()))
        throw std::runtime_error("Choose a dedicated export directory outside the project root.");
    if (fs::exists(output.parent_path())) {
        for (const auto& entry : fs::directory_iterator(output.parent_path())) {
            const auto name = entry.path().filename().string();
            if (name != "linux" && name != "windows")
                throw std::runtime_error("Choose an empty export directory; unrelated files exist: " + entry.path().string());
            validate_destination(entry.path());
        }
    }
    auto main = read_json(project);
    if (!main.HasMember("format") || !main["format"].IsString())
        throw std::runtime_error("Select a saved Bubble Engine project or scene.");
    const std::string format = main["format"].GetString();
    if (format != "bubble-engine-project" && format != "bubble-engine-scene")
        throw std::runtime_error("Unsupported game project format.");
    preparation paths(root, repository / "commons/assets");
    if (main.HasMember("scene")) {
        if (!main["scene"].IsString()) throw std::runtime_error("Invalid main scene path.");
        const auto scene = paths.asset(main["scene"].GetString());
        if (scene.starts_with("../")) throw std::runtime_error("Main scene must be inside the project.");
        read_json(root / scene);
        main["scene"].SetString(scene.c_str(), static_cast<rapidjson::SizeType>(scene.size()), main.GetAllocator());
    }
    fs::remove_all(staging);
    fs::create_directories(staging / "game");
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        const auto relative = it->path().lexically_relative(root);
        const auto canonical = fs::weakly_canonical(it->path());
        if (excluded(relative) || within(canonical, destination.parent_path())) {
            if (it->is_directory()) it.disable_recursion_pending();
            continue;
        }
        if (!within(canonical, root))
            throw std::runtime_error("Import external symlink into the project: " + it->path().string());
        if (it->is_symlink() && it->is_directory())
            throw std::runtime_error("Import linked directories before exporting: " + it->path().string());
        if (!it->is_regular_file()) continue;
        const auto target = staging / "game" / relative;
        fs::create_directories(target.parent_path());
        auto extension = relative.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (it->path() == project || extension == ".bengine" || extension == ".scene" || extension == ".world") {
            auto document = it->path() == project ? std::move(main) : read_json(it->path());
            paths.rewrite(document, document.GetAllocator());
            write_json(target, document);
        } else if (extension == ".gltf" || extension == ".obj" || extension == ".mtl" || extension == ".bmodel" || extension == ".bmat") {
            paths.model_document(it->path(), target);
        } else {
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing);
        }
    }
    fs::copy(repository / "external/cpp-bgui/assets", staging / "assets", fs::copy_options::recursive);
    fs::copy(repository / "commons/assets", staging / "assets",
             fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    std::string name = root.filename().string();
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') c = '_';
    if (name.empty()) name = "Game";
    rapidjson::Document manifest;
    manifest.SetObject();
    auto& a = manifest.GetAllocator();
    manifest.AddMember("format", "bubble-engine-game", a);
    manifest.AddMember("version", 1, a);
    const auto entry = (fs::path("game") / project.filename()).generic_string();
    manifest.AddMember("project", rapidjson::Value(entry.c_str(), a), a);
    manifest.AddMember("executable", rapidjson::Value(name.c_str(), a), a);
    write_json(staging / "game.json", manifest);
}

inline void stage_binary(const fs::path& binary, const fs::path& staging, bool windows) {
    auto manifest = read_json(staging / "game.json");
    const std::string name = manifest["executable"].GetString();
    const auto filename = name + (windows ? ".exe" : "");
    fs::copy_file(binary, staging / filename, fs::copy_options::overwrite_existing);
    if (windows) {
        for (const auto& entry : fs::directory_iterator(binary.parent_path())) {
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (entry.is_regular_file() && extension == ".dll")
                fs::copy_file(entry.path(), staging / entry.path().filename(), fs::copy_options::overwrite_existing);
        }
    }
    if (manifest.HasMember("binary")) {
        const fs::path previous(manifest["binary"].GetString());
        if (previous.filename() == previous && previous != filename) fs::remove(staging / previous);
        manifest.RemoveMember("binary");
    }
    manifest.AddMember("binary", rapidjson::Value(filename.c_str(), manifest.GetAllocator()), manifest.GetAllocator());
    write_json(staging / "game.json", manifest);
}

inline void publish(const fs::path& staging, const fs::path& output, bool single = false) {
    auto manifest = read_json(staging / "game.json");
    if (!game_manifest(manifest) || !manifest.HasMember("binary") || !manifest["binary"].IsString())
        throw std::runtime_error("The game executable has not been staged.");
    const fs::path binary(manifest["binary"].GetString());
    if (binary.filename() != binary || !fs::is_regular_file(staging / binary))
        throw std::runtime_error("The game executable is missing.");
    auto source = staging;
    if (single) {
        if (fs::exists(staging / "lib") && !fs::is_empty(staging / "lib"))
            throw std::runtime_error("Single executable requires a static runtime template, without companion libraries.");
        for(const auto& file:fs::directory_iterator(staging)) {
            auto ext=file.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
            if(ext==".dll")throw std::runtime_error("Single executable requires a static runtime template, without DLLs.");
        }
        source=staging.parent_path()/"single-executable";
        fs::remove_all(source);fs::create_directories(source);
        executable_archive::append(staging/binary,staging,binary.generic_string(),source/binary);
    }
    validate_destination(output);
    fs::create_directories(output.parent_path());
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto pending = output.parent_path() / (".bengine-package-" + token);
    const auto backup = output.parent_path() / (".bengine-previous-" + token);
    try {
        fs::copy(source, pending, fs::copy_options::recursive);
        if (fs::exists(output)) fs::rename(output, backup);
        try { fs::rename(pending, output); }
        catch (...) {
            if (fs::exists(backup)) fs::rename(backup, output);
            throw;
        }
        fs::remove_all(backup);
    } catch (...) {
        fs::remove_all(pending);
        throw;
    }
}
}
