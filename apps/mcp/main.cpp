#include "engine_version.hpp"
#include "project_snapshot.hpp"
#include "creative_service.hpp"
#include "tool_catalog.hpp"
#include <rapidjson/schema.h>
#include <fstream>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace bubble::mcp {
namespace {
using rapidjson::Document;
using rapidjson::Value;
using allocator = Document::AllocatorType;
namespace fs = std::filesystem;

constexpr auto protocol_version = "2025-11-25";
constexpr unsigned max_page_size = 100;


const Value& field(const Value& object, const char* key) {
    static const Value missing;
    if (object.IsObject()) {
        const auto member = object.FindMember(key);
        if (member != object.MemberEnd())
            return member->value;
    }
    return missing;
}

std::string_view as_text(const Value& value) {
    if (value.IsString())
        return {value.GetString(), value.GetStringLength()};
    return {};
}

Value owned_text(std::string_view text, allocator& memory) {
    return Value(text.data(), static_cast<rapidjson::SizeType>(text.size()), memory);
}

void copy_field(Value& target, const Value& source, const char* key, allocator& memory) {
    const auto member = source.FindMember(key);
    if (member != source.MemberEnd())
        target.AddMember(Value(rapidjson::StringRef(key)), Value(member->value, memory), memory);
}

std::string serialize(const Value& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer writer(buffer);
    value.Accept(writer);
    return {buffer.GetString(), buffer.GetSize()};
}

Value tool_result(Value data, allocator& memory, const char* error = nullptr) {
    const auto text = error ? std::string(error) : serialize(data);
    Value item(rapidjson::kObjectType);
    item.AddMember("type", "text", memory);
    item.AddMember("text", owned_text(text, memory), memory);
    Value content(rapidjson::kArrayType);
    content.PushBack(item, memory);
    Value result(rapidjson::kObjectType);
    result.AddMember("content", content, memory);
    if (error)
        result.AddMember("isError", true, memory);
    else
        result.AddMember("structuredContent", data, memory);
    return result;
}

Value inspect_project(const fs::path& path, std::string_view tool, unsigned argument, allocator& memory) {
    const project_snapshot snapshot(path);
    const auto& entities = snapshot.entities();
    Value data(rapidjson::kObjectType);
    if (tool == "project_info") {
        for (const auto key : {"format", "version", "project_version", "engine_version",
                "minimum_engine_version", "scene", "settings"})
            copy_field(data, snapshot.manifest, key, memory);
        data.AddMember("entityCount", entities.Size(), memory);
    } else if (tool == "list_entities") {
        Value page(rapidjson::kArrayType);
        const auto first = std::min(argument, entities.Size());
        const auto end = first + std::min(max_page_size, entities.Size() - first);
        for (auto index = first; index < end; ++index) {
            const auto& entity = entities[index];
            Value summary(rapidjson::kObjectType);
            for (const auto key : {"id", "name", "parent", "hidden", "locked"})
                copy_field(summary, entity, key, memory);
            Value types(rapidjson::kArrayType);
            for (const auto& component : entity["components"].GetObject())
                types.PushBack(Value(component.name, memory), memory);
            summary.AddMember("componentTypes", types, memory);
            page.PushBack(summary, memory);
        }
        data.AddMember("entities", page, memory);
        data.AddMember("total", entities.Size(), memory);
        if (end < entities.Size())
            data.AddMember("nextOffset", end, memory);
    } else {
        for (const auto& entity : entities.GetArray()) {
            if (entity["id"].GetUint() == argument)
                return tool_result(Value(entity, memory), memory);
        }
        return tool_result({}, memory, "Entity ID was not found in the saved scene.");
    }
    return tool_result(std::move(data), memory);
}

enum class phase { connecting, initializing, ready };

class server {
public:
    server(fs::path project, fs::path workspace, fs::path runtime)
        : m_service(std::move(project), std::move(workspace), std::move(runtime)) {}

    void dispatch(std::string_view line) {
        unsigned depth = 0;
        bool quoted = false, escaped = false, too_deep = false;
        for (char character : line) {
            if (quoted) {
                if (escaped) escaped = false;
                else if (character == '\\') escaped = true;
                else if (character == '"') quoted = false;
            } else if (character == '"') quoted = true;
            else if (character == '{' || character == '[') { if (++depth > 64) too_deep = true; }
            else if ((character == '}' || character == ']') && depth) --depth;
        }
        if (too_deep) line = "{";
        Document request;
        request.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(
            line.data(), line.size());
        Document response(rapidjson::kObjectType);
        auto& memory = response.GetAllocator();
        response.AddMember("jsonrpc", "2.0", memory);

        const auto fail = [&](int code, const char* message) {
            Value error(rapidjson::kObjectType);
            error.AddMember("code", code, memory);
            error.AddMember("message", Value(rapidjson::StringRef(message)), memory);
            response.AddMember("error", error, memory);
        };

        const auto& id = field(request, "id");
        const auto& method = field(request, "method");
        const auto& params = field(request, "params");
        const bool valid_id = id.IsString() || id.IsInt64() || id.IsUint64();
        response.AddMember("id", valid_id ? Value(id, memory) : Value(), memory);

        if (request.HasParseError() || line.find('\0') != std::string::npos) {
            response["id"].SetNull();
            fail(-32700, "Parse error");
        } else if (as_text(field(request, "jsonrpc")) != "2.0" || !method.IsString() ||
                   (request.HasMember("id") && !valid_id)) {
            fail(-32600, "Invalid request");
        } else if (!valid_id) {
            if (m_phase == phase::initializing && as_text(method) == "notifications/initialized" &&
                (!request.HasMember("params") || params.IsObject()))
                m_phase = phase::ready;
            return;
        } else if (request.HasMember("params") && !params.IsObject()) {
            fail(-32602, "Expected object parameters");
        } else if (as_text(method) == "initialize") {
            const auto& client = field(params, "clientInfo");
            if (m_phase != phase::connecting) {
                fail(-32600, "Server has already been initialized");
            } else if (!field(params, "protocolVersion").IsString() ||
                       !field(params, "capabilities").IsObject() ||
                       !field(client, "name").IsString() || !field(client, "version").IsString()) {
                fail(-32602, "Expected protocolVersion, capabilities and clientInfo");
            } else {
                Value result(rapidjson::kObjectType);
                Value capabilities(rapidjson::kObjectType);
                capabilities.AddMember("tools", Value(rapidjson::kObjectType), memory);
                Value info(rapidjson::kObjectType);
                info.AddMember("name", "bubble-engine", memory);
                info.AddMember("version", BUBBLE_ENGINE_VERSION, memory);
                result.AddMember("protocolVersion", Value(rapidjson::StringRef(protocol_version)), memory);
                result.AddMember("capabilities", capabilities, memory);
                result.AddMember("serverInfo", info, memory);
                response.AddMember("result", result, memory);
                m_phase = phase::initializing;
            }
        } else if (as_text(method) == "ping") {
            response.AddMember("result", Value(rapidjson::kObjectType), memory);
        } else if (as_text(method) == "server/discover") {
            fail(-32601, "Method not found");
        } else if (m_phase != phase::ready) {
            fail(-32002, "Server not initialized");
        } else if (as_text(method) == "tools/list") {
            Document catalog;
            catalog.Parse(tool_catalog);
            response.AddMember("result", Value(catalog, memory), memory);
        } else if (as_text(method) == "tools/call") {
            const auto tool = as_text(field(params, "name"));
            const auto& arguments = field(params, "arguments");
            Document catalog;
            catalog.Parse(tool_catalog);
            const Value* definition = nullptr;
            for (const auto& item : catalog["tools"].GetArray())
                if (as_text(item["name"]) == tool) definition = &item;
            Document empty(rapidjson::kObjectType);
            const auto& input = arguments.IsNull() ? static_cast<const Value&>(empty) : arguments;
            if (!definition) fail(-32602, "Unknown tool");
            else {
                rapidjson::SchemaDocument schema((*definition)["inputSchema"]);
                rapidjson::SchemaValidator validator(schema);
                if (!input.Accept(validator) || (params.HasMember("arguments") && !arguments.IsObject()))
                    fail(-32602, "Invalid tool arguments");
                else try {
                    Value result;
                    if (tool == "project_info" || tool == "list_entities" || tool == "get_entity") {
                        const auto& argument = field(input, tool == "get_entity" ? "id" : "offset");
                        result = inspect_project(m_service.project(), tool,
                            argument.IsUint() ? argument.GetUint() : 0u, memory);
                    } else {
                        struct redirect {
                            std::streambuf* previous = std::cout.rdbuf(std::cerr.rdbuf());
                            ~redirect() { std::cout.rdbuf(previous); }
                        } output;
                        auto data = m_service.call(tool, input, memory);
                        result = tool_result(std::move(data), memory);
                        if (tool == "test_status" && result["structuredContent"].HasMember("report")) {
                            const auto size = fs::file_size(m_service.capture());
                            if (size > 16 * 1024 * 1024) throw std::runtime_error("Capture exceeds 16 MiB.");
                            std::ifstream file(m_service.capture(), std::ios::binary);
                            std::string bytes(static_cast<std::size_t>(size), '\0');
                            if (!file.read(bytes.data(), static_cast<std::streamsize>(size)))
                                throw std::runtime_error("Could not read complete capture.");
                            if (!file.bad() && !bytes.empty() && bytes.size() <= 16 * 1024 * 1024) {
                                constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                                std::string encoded;
                                unsigned value = 0; int bits = -6;
                                for (unsigned char byte : bytes) {
                                    value = (value << 8) | byte; bits += 8;
                                    while (bits >= 0) { encoded += alphabet[(value >> bits) & 63]; bits -= 6; }
                                }
                                if (bits > -6) encoded += alphabet[((value << 8) >> (bits + 8)) & 63];
                                while (encoded.size() % 4) encoded += '=';
                                Value image(rapidjson::kObjectType);
                                image.AddMember("type", "image", memory).AddMember("mimeType", "image/png", memory);
                                image.AddMember("data", owned_text(encoded, memory), memory);
                                result["content"].PushBack(image, memory);
                            }
                        }
                    }
                    response.AddMember("result", result, memory);
                } catch (const std::exception& error) {
                    response.AddMember("result", tool_result({}, memory, error.what()), memory);
                }
            }
        } else {
            fail(-32601, "Method not found");
        }

        std::cout << serialize(response) << '\n' << std::flush;
    }

private:
    creative_service m_service;
    phase m_phase = phase::connecting;
};
}
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path project, workspace, runtime;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return 2;
        const std::string_view key(argv[i]);
        if (key == "--project" && project.empty()) project = argv[i + 1];
        else if (key == "--workspace" && workspace.empty()) workspace = argv[i + 1];
        else if (key == "--runtime" && runtime.empty()) runtime = argv[i + 1];
        else return 2;
    }
    try {
        if (project.empty() && workspace.empty()) throw std::runtime_error("Configure --project or --workspace.");
        if (!project.empty()) {
            project = fs::canonical(project);
            if (project.extension() != ".bengine" || !fs::is_regular_file(project))
                throw std::runtime_error("Expected a .bengine file.");
        }
        if (!workspace.empty()) {
            workspace = fs::canonical(workspace);
            if (!fs::is_directory(workspace)) throw std::runtime_error("Expected an existing workspace directory.");
            if (!project.empty()) {
                const auto relative = project.lexically_relative(workspace);
                if (relative.empty() || *relative.begin() == "..") throw std::runtime_error("Project is outside workspace.");
            }
        }
        if (!runtime.empty()) {
            runtime = fs::canonical(runtime);
            if (!fs::is_regular_file(runtime)) throw std::runtime_error("Expected a runtime executable.");
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
    bubble::mcp::server server(project, workspace, runtime);
    constexpr std::size_t max_message_bytes = 1024 * 1024;
    std::string buffer(max_message_bytes + 2, '\0');
    while (std::cout && std::cin.getline(buffer.data(), buffer.size())) {
        const auto length = static_cast<std::size_t>(std::cin.gcount()) - (std::cin.eof() ? 0 : 1);
        if (length > max_message_bytes) {
            std::cerr << "MCP message exceeds the 1 MiB limit.\n";
            return 1;
        }
        server.dispatch(std::string_view(buffer.data(), length));
    }
    if (!std::cin.eof() && !std::cin.bad() && std::cout) {
        std::cerr << "MCP message exceeds the 1 MiB limit.\n";
        return 1;
    }
    return std::cin.bad() || !std::cout;
}
