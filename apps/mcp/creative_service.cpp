#include "creative_service.hpp"
#include "project_snapshot.hpp"
#include "editor/systems/project_creation_system.hpp"
#include "project/project_file_transaction.hpp"
#include "project/project_persistence_system.hpp"
#include "resources/animation_clip.hpp"
#include "loaders/editable_model.hpp"
#include <lua.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>

namespace bubble::mcp {
namespace {
namespace fs = std::filesystem;
namespace files = editor::ui::project_files;
using rapidjson::Value;
using rapidjson::Document;
using Allocator = Document::AllocatorType;
const Value& field(const Value& value, const char* key) {
    static const Value empty;
    return value.IsObject() && value.HasMember(key) ? value[key] : empty;
}
std::string text(const Value& value) {
    if (!value.IsString()) throw std::runtime_error("Expected a string.");
    std::string result(value.GetString(), value.GetStringLength());
    if (result.find('\0') != std::string::npos) throw std::runtime_error("NUL is not allowed.");
    return result;
}
Value str(const std::string& value, Allocator& a) { return Value(value.c_str(), value.size(), a); }
bool inside(const fs::path& child, const fs::path& root) {
    const auto relative = child.lexically_relative(root);
    return !relative.empty() && relative != "." && *relative.begin() != "..";
}
fs::path confined(const fs::path& root, const std::string& name) {
    const fs::path relative(name);
    if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Use a relative path.");
    auto current = root;
    for (const auto& part : relative) {
        if (part == ".." || part == ".") throw std::runtime_error("Path traversal is not allowed.");
        current /= part;
        if (fs::is_symlink(fs::symlink_status(current))) throw std::runtime_error("Symlinks are not allowed.");
    }
    if (!inside(current.lexically_normal(), root)) throw std::runtime_error("Path escapes workspace.");
    return current.lexically_normal();
}
std::string read(const fs::path& file) {
    if (!fs::is_regular_file(file) || fs::file_size(file) > 32 * 1024 * 1024)
        throw std::runtime_error("Expected a regular file of at most 32 MiB.");
    return files::read(file);
}
void validate_json(const Value& value, unsigned depth = 64) {
    if (!depth) throw std::runtime_error("JSON nesting exceeds 64 levels.");
    if (value.IsObject()) {
        std::set<std::string> names;
        for (const auto& member : value.GetObject()) {
            if (!names.insert(text(member.name)).second) throw std::runtime_error("Duplicate JSON key.");
            validate_json(member.value, depth - 1);
        }
    } else if (value.IsArray()) for (const auto& child : value.GetArray()) validate_json(child, depth - 1);
}
Document parse(const std::string& bytes) {
    Document doc;
    doc.Parse<rapidjson::kParseIterativeFlag | rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
    if (doc.HasParseError() || !doc.IsObject()) throw std::runtime_error("Expected a JSON object.");
    validate_json(doc);
    return doc;
}
void validate_scene(const Value& doc) {
    std::string error;
    if (text(field(doc, "format")) != "bubble-engine-scene" || !field(doc, "version").IsUint() ||
        doc["version"].GetUint() != 1 || !editor::ui::validate_scene_entities(field(doc, "entities"), error))
        throw std::runtime_error("Invalid scene: " + error);
}
unsigned number(const Value& args, const char* name, unsigned fallback, unsigned low, unsigned high) {
    const auto& value = field(args, name);
    if (value.IsNull()) return fallback;
    if (!value.IsUint() || value.GetUint() < low || value.GetUint() > high)
        throw std::runtime_error("Invalid numeric argument: " + std::string(name));
    return value.GetUint();
}
void references(const Value& value, const fs::path& root) {
    if (value.IsObject()) for (const auto& member : value.GetObject()) {
        const auto key = text(member.name);
        if ((key == "model" || key == "file" || key == "clip" || key == "material" || key == "directory") && member.value.IsString()) {
            const auto name = text(member.value);
            if (!name.empty()) confined(root, name);
        }
        references(member.value, root);
    }
    else if (value.IsArray()) for (const auto& item : value.GetArray()) references(item, root);
}
}

creative_service::creative_service(fs::path project, fs::path workspace, fs::path runtime)
    : m_project(std::move(project)), m_workspace(std::move(workspace)), m_runtime(std::move(runtime)) {}
fs::path creative_service::path(const std::string& relative) const {
    if (m_project.empty()) throw std::runtime_error("Create or open a project first.");
    return confined(m_project.parent_path(), relative);
}
void creative_service::write(const fs::path& target, const std::string& contents, const Value& args) {
    std::string process_error;
    if (m_process.running(process_error)) throw std::runtime_error("Stop the active test before editing assets.");
    if (!process_error.empty()) throw std::runtime_error(process_error);
    if (fs::exists(files::journal_path(target)))
        throw std::runtime_error("Pending save transaction; recover in the editor before editing.");
    if (fs::exists(target)) {
        if (!field(args, "expectedContents").IsString() || text(args["expectedContents"]) != read(target))
            throw std::runtime_error("File changed or expectedContents missing. Read the file and retry.");
    } else if (!field(args, "expectedContents").IsNull())
        throw std::runtime_error("File no longer exists.");
    fs::create_directories(target.parent_path());
    std::string error;
    if (!files::save(target, {{target, contents}}, error)) throw std::runtime_error(error);
}
fs::path creative_service::capture() const { return m_test.empty() ? fs::path{} : fs::path(m_test.string() + ".preview.png"); }

Value creative_service::call(std::string_view name, const Value& args, Allocator& a) {
    Value result(rapidjson::kObjectType);
    if (name == "authoring_guide") {
        auto guide = parse(R"json({
            "workflow":["create_project or open_project", "create_model / write_asset", "read_file then compose_scene", "set_main_scene", "run_test then poll test_status and review PNG against specification"],
            "editing":"Saved files only. Close editor or reload after MCP changes. Existing file edits require exact expectedContents. Lua scripts execute only in runtime tests.",
            "entityExample":{"id":1,"name":"Animated cube","components":{"transform":{"position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]},"renderer":{"model":"assets/models/cube.bmodel","materials":[{"albedo":[0.2,0.5,1,1]}]},"animation":{"clip":"assets/animations/spin.banim","autoplay":true,"speed":1},"code":{"directory":"scripts/example.lua"}}},
            "cameraExample":{"id":2,"name":"Camera","components":{"transform":{"position":[0,1,5],"rotation":[0,-90,0],"scale":[1,1,1]},"camera":{}}},
            "animationExample":{"format":"bubble-animation-clip","version":1,"duration":2,"loop":true,"tracks":[{"component":8,"property":"rotation.y","stepped":false,"keys":[[0,0],[2,360]]}]},
            "modelExample":{"format":"bengine-model","version":1,"meshes":[{"vertices":[[-0.5,0,-0.5,0,0],[0.5,0,-0.5,1,0],[0.5,0,0.5,1,1],[-0.5,0,0.5,0,1]],"faces":[[0,3,2,1]]}]},
            "scriptExample":"function setup()\n    Debug.Log(\"Scene ready\")\nend\n\nfunction update()\nend\n",
            "capture":"Camera framebuffer only; HUD excluded. width/height 64..2048, frames 1..3600. Real-time simulation, not deterministic. Reports do not automatically verify visual expectations. Artifacts persist in the project captures directory. PNG is also returned as MCP image content."
        })json");
        return Value(guide, a);
    } else if (name == "create_model") {
        if (path(text(field(args, "path"))).extension() != ".bmodel") throw std::runtime_error("Expected .bmodel path.");
        auto model = parse(R"({"format":"bengine-model","version":1,"meshes":[{"vertices":[[-0.5,-0.5,-0.5,0,0],[0.5,-0.5,-0.5,1,0],[0.5,0.5,-0.5,1,1],[-0.5,0.5,-0.5,0,1],[-0.5,-0.5,0.5,0,0],[0.5,-0.5,0.5,1,0],[0.5,0.5,0.5,1,1],[-0.5,0.5,0.5,0,1]],"faces":[[0,3,2,1],[4,5,6,7],[0,4,7,3],[1,2,6,5],[0,1,5,4],[3,7,6,2]]}]})");
        if (text(field(args, "shape")) == "plane") {
            model = parse(R"({"format":"bengine-model","version":1,"meshes":[{"vertices":[[-0.5,0,-0.5,0,0],[0.5,0,-0.5,1,0],[0.5,0,0.5,1,1],[-0.5,0,0.5,0,1]],"faces":[[0,3,2,1]]}]})");
        }
        Document request;
        request.CopyFrom(args, request.GetAllocator());
        request.AddMember("contents", str(files::encode(model), request.GetAllocator()), request.GetAllocator());
        return call("write_asset", request, a);
    } else if (name == "compose_scene") {
        const auto target = path(text(field(args, "path")));
        if (target.extension() != ".scene" && target.extension() != ".world") throw std::runtime_error("Expected scene path.");
        auto scene = fs::exists(target) ? parse(read(target)) :
            parse(R"({"format":"bubble-engine-scene","version":1,"entities":[]})");
        validate_scene(scene);
        auto& memory = scene.GetAllocator();
        auto& entities = scene["entities"];
        const auto& removals = field(args, "removeIds");
        if (removals.IsArray()) for (const auto& id : removals.GetArray())
            for (auto it = entities.Begin(); it != entities.End();) {
                if ((*it)["id"].GetUint() == id.GetUint()) it = entities.Erase(it);
                else ++it;
            }
        std::set<unsigned> updated;
        for (const auto& entity : args["entities"].GetArray()) {
            const auto& id = field(entity, "id");
            if (!id.IsUint() || !id.GetUint() || !updated.insert(id.GetUint()).second)
                throw std::runtime_error("Expected unique positive entity IDs.");
            bool replaced = false;
            for (auto& existing : entities.GetArray()) if (existing["id"].GetUint() == id.GetUint()) {
                existing.CopyFrom(entity, memory); replaced = true; break;
            }
            if (!replaced) entities.PushBack(Value(entity, memory), memory);
        }
        validate_json(scene);
        validate_scene(scene);
        references(scene, m_project.parent_path());
        write(target, files::encode(scene), args);
        result.AddMember("entityCount", entities.Size(), a);
    } else if (name == "create_project" || name == "open_project") {
        if (m_workspace.empty()) throw std::runtime_error("Start MCP with --workspace to create or switch projects.");
        std::string error;
        if (m_process.running(error)) throw std::runtime_error("Stop the current test before switching projects.");
        const auto target = confined(m_workspace, text(field(args, "path")));
        if (name == "create_project") {
            const auto preset = text(field(args, "template"));
            if (preset != "empty" && preset != "fpp") throw std::runtime_error("Choose empty or fpp template.");
            if (!editor::ui::create_project_files(target, error, preset == "fpp" ?
                editor::ui::project_template::fpp : editor::ui::project_template::empty)) throw std::runtime_error(error);
            m_project = target / "project.bengine";
        } else {
            if (target.extension() != ".bengine") throw std::runtime_error("Expected .bengine project.");
            project_snapshot checked(target);
            m_project = target;
        }
        m_test.clear();
        result.AddMember("project", str(m_project.string(), a), a);
    } else if (name == "list_files") {
        if (m_project.empty()) throw std::runtime_error("Open a project first.");
        Value paths(rapidjson::kArrayType);
        std::vector<std::string> names;
        for (const auto& entry : fs::recursive_directory_iterator(m_project.parent_path())) {
            if (fs::is_regular_file(entry.symlink_status()))
                names.push_back(entry.path().lexically_relative(m_project.parent_path()).generic_string());
            if (names.size() > 10000) throw std::runtime_error("Project exceeds the 10000 file listing limit.");
        }
        std::sort(names.begin(), names.end());
        const auto offset = number(args, "offset", 0, 0, 10000);
        const auto end = std::min<std::size_t>(names.size(), offset + 100);
        for (auto i = std::min<std::size_t>(offset, names.size()); i < end; ++i) paths.PushBack(str(names[i], a), a);
        result.AddMember("files", paths, a);
        if (end < names.size()) result.AddMember("nextOffset", static_cast<unsigned>(end), a);
    } else if (name == "read_file") {
        const auto target = path(text(field(args, "path")));
        const auto bytes = read(target);
        if (bytes.size() > 400000 || bytes.find('\0') != std::string::npos)
            throw std::runtime_error("read_file supports text files up to 400000 bytes.");
        result.AddMember("contents", str(bytes, a), a);
    } else if (name == "write_asset") {
        const auto target = path(text(field(args, "path")));
        const auto bytes = text(field(args, "contents"));
        if (bytes.size() > 400000) throw std::runtime_error("Asset exceeds 400000 bytes.");
        const auto ext = target.extension();
        if (ext == ".lua") {
            auto* state = luaL_newstate();
            if (!state) throw std::runtime_error("Could not allocate Lua parser.");
            const int status = luaL_loadbufferx(state, bytes.data(), bytes.size(), target.filename().string().c_str(), "t");
            std::string error = status ? lua_tostring(state, -1) : "";
            lua_close(state);
            if (status) throw std::runtime_error(error);
        } else {
            auto doc = parse(bytes);
            references(doc, m_project.parent_path());
            if (ext == ".scene" || ext == ".world") validate_scene(doc);
            else if (ext == ".banim") {
                commons::animation_clip clip;
                if (!clip.analyze(doc)) throw std::runtime_error("Invalid .banim animation clip.");
            } else if (ext == ".bmodel") {
                std::string error;
                const auto temporary = m_process.snapshot_path(error);
                if (temporary.empty()) throw std::runtime_error(error);
                const auto staged = temporary.parent_path() / "validate.bmodel";
                files::stage({staged, bytes}); files::replace(staged);
                commons::editable_model_data model;
                if (!commons::load_editable_model(staged, model, error)) throw std::runtime_error(error);
            } else throw std::runtime_error("Supported editable assets: .lua, .scene, .world, .banim, .bmodel.");
        }
        write(target, bytes, args);
        result.AddMember("path", str(target.string(), a), a);
    } else if (name == "set_main_scene") {
        const auto scene = path(text(field(args, "path")));
        validate_scene(parse(read(scene)));
        auto project = parse(read(m_project));
        project.RemoveMember("scene");
        project.AddMember("scene", str(scene.lexically_relative(m_project.parent_path()).generic_string(), project.GetAllocator()), project.GetAllocator());
        write(m_project, files::encode(project), args);
        result.AddMember("scene", str(scene.string(), a), a);
    } else if (name == "run_test") {
        if (m_runtime.empty()) throw std::runtime_error("Configure a trusted runtime executable with --runtime.");
        if (m_project.empty()) throw std::runtime_error("Open a project first.");
        std::string error;
        if (m_process.running(error)) throw std::runtime_error("A test is already running.");
        const auto specification = text(field(args, "specification"));
        if (specification.empty() || specification.size() > 8192) throw std::runtime_error("Provide a specification up to 8192 characters.");
        const unsigned width = number(args, "width", 1280, 64, 2048);
        const unsigned height = number(args, "height", 720, 64, 2048);
        const unsigned frames = number(args, "frames", 120, 1, 3600);
        const unsigned timeout = number(args, "timeoutSeconds", 30, 1, 120);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_test = path("captures/test-" + std::to_string(stamp) + ".json");
        fs::create_directories(m_test.parent_path());
        Document spec(rapidjson::kObjectType);
        auto& memory = spec.GetAllocator();
        spec.AddMember("timeoutSeconds", timeout, memory);
        spec.AddMember("width", width, memory).AddMember("height", height, memory).AddMember("frames", frames, memory);
        spec.AddMember("specification", str(specification, memory), memory);
        spec.AddMember("project", str(m_project.string(), memory), memory);
        files::stage({m_test, files::encode(spec)}); files::replace(m_test);
        if (!m_process.start(m_runtime, m_project, error, {true, m_test})) throw std::runtime_error(error);
        m_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        m_timed_out = false;
        result.AddMember("started", true, a);
    } else if (name == "test_status" || name == "stop_test") {
        if (m_test.empty()) throw std::runtime_error("No test has been started.");
        std::string error;
        bool running = m_process.running(error);
        if (!error.empty()) throw std::runtime_error(error);
        if (running && (name == "stop_test" || std::chrono::steady_clock::now() >= m_deadline)) {
            m_timed_out = name != "stop_test";
            if (!m_process.stop(error)) throw std::runtime_error(error);
            running = false;
        }
        if (!running && m_process.exit_code() == 124) m_timed_out = true;
        result.AddMember("running", running, a).AddMember("timedOut", m_timed_out, a);
        result.AddMember("exitCode", m_process.exit_code(), a);
        const auto log = fs::path(m_test.string() + ".log");
        if (fs::exists(log)) {
            std::ifstream stream(log, std::ios::binary);
            const auto size = fs::file_size(log);
            stream.seekg(size > 65536 ? size - 65536 : 0);
            std::string tail(static_cast<std::size_t>(std::min<std::uintmax_t>(size, 65536)), '\0');
            stream.read(tail.data(), static_cast<std::streamsize>(tail.size()));
            tail.resize(static_cast<std::size_t>(stream.gcount()));
            result.AddMember("log", str(tail, a), a);
        }
        const auto report = fs::path(m_test.string() + ".report.json");
        if (!running && fs::exists(report)) {
            auto doc = parse(read(report));
            result.AddMember("report", Value(doc, a), a);
            result.AddMember("capture", str(capture().string(), a), a);
        }
    } else throw std::runtime_error("Unknown creative tool.");
    return result;
}
}
