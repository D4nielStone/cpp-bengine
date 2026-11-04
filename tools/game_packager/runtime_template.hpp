#pragma once
#include "game_package.hpp"
#include <iomanip>
#include <set>
#include <sstream>

namespace game_package {
inline std::string template_key(const fs::path& repository,const std::string& target,const std::string& format) {
    std::vector<fs::path> files;
    for(const auto& relative:{"commons","external/cpp-bgui/core","external/cpp-bgui/backend","external/glad",
            "external/rapidjson/include","external/sol2/include","apps/runtime","apps/project"}) {
        const auto folder=repository/relative;
        if(!fs::exists(folder)) continue;
        for(fs::recursive_directory_iterator it(folder),end;it!=end;++it) {
            if(excluded(it->path().lexically_relative(repository)) && it->is_directory()) {it.disable_recursion_pending();continue;}
            if(!it->is_regular_file())continue;
            const auto ext=it->path().extension().string();
            if(ext==".h" || ext==".hpp" || ext==".cpp" || ext==".c" || ext==".cmake" || it->path().filename()=="CMakeLists.txt") files.push_back(it->path());
        }
    }
    for(const auto* relative:{"CMakeLists.txt","apps/CMakeLists.txt","apps/runtime_app.cpp","external/cpp-bgui/CMakeLists.txt","scripts/compile_runtime.sh","scripts/compile_runtime.ps1"})
        if(fs::exists(repository/relative)) files.push_back(repository/relative);
    std::sort(files.begin(),files.end());
    std::uint64_t hash=14695981039346656037ull;
    const auto add=[&](unsigned char c){hash^=c;hash*=1099511628211ull;};
    for(char c:std::string("BubbleEngine-template-v3|")+target+"|"+format) add(c);
    for(const auto& file:files) {
        for(char c:file.lexically_relative(repository).generic_string()) add(c);
        add(0);
        std::ifstream input(file,std::ios::binary);
        for(char c;input.get(c);) if(c!='\r')add(c); // Same key on LF and CRLF checkouts.
        if(input.bad())throw std::runtime_error("Cannot fingerprint runtime source.");
        add(0);
    }
    std::ostringstream key;key<<std::hex<<std::setw(16)<<std::setfill('0')<<hash;return key.str();
}
inline void validate_template(const fs::path& folder,const std::string& key,const std::string& target,const std::string& format) {
    auto manifest=read_json(folder/"template.json");
    for(const auto* field:{"engine_key","target","format"})
        if(!manifest.HasMember(field) || !manifest[field].IsString())throw std::runtime_error("Invalid runtime template metadata.");
    if(manifest["engine_key"].GetString()!=key || manifest["target"].GetString()!=target || manifest["format"].GetString()!=format)
        throw std::runtime_error("Runtime template does not match this engine, platform or export format.");
    if(!manifest.HasMember("files") || !manifest["files"].IsArray() || manifest["files"].Empty())
        throw std::runtime_error("Runtime template file inventory is missing.");
    for(const auto& file:manifest["files"].GetArray()) {
        if(!file.IsObject() || !file.HasMember("path") || !file["path"].IsString() ||
            !file.HasMember("size") || !file["size"].IsUint64() || !executable_archive::safe_name(file["path"].GetString()) ||
            !fs::is_regular_file(folder/file["path"].GetString()) || fs::file_size(folder/file["path"].GetString())!=file["size"].GetUint64())
            throw std::runtime_error("Runtime template is incomplete or damaged.");
    }
    if(!fs::is_regular_file(folder/(target=="windows"?"runtime_app.exe":"runtime_app")))
        throw std::runtime_error("Runtime template executable is missing.");
}
inline void create_template(const fs::path& binary,const fs::path& folder,const std::string& key,const std::string& target,const std::string& format) {
    if(target!="linux" && target!="windows")throw std::runtime_error("Unsupported runtime template target.");
    if(format!="folder" && format!="single")throw std::runtime_error("Unsupported runtime template format.");
    fs::create_directories(folder);
    fs::copy_file(binary,folder/(target=="windows"?"runtime_app.exe":"runtime_app"),fs::copy_options::overwrite_existing);
    if(target=="windows") for(const auto& file:fs::directory_iterator(binary.parent_path())) {
        auto ext=file.path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
        if(ext==".dll") fs::copy_file(file.path(),folder/file.path().filename(),fs::copy_options::overwrite_existing);
    }
    rapidjson::Document manifest;manifest.SetObject();auto& a=manifest.GetAllocator();
    manifest.AddMember("engine_key",rapidjson::Value(key.c_str(),a),a);
    manifest.AddMember("target",rapidjson::Value(target.c_str(),a),a);
    manifest.AddMember("format",rapidjson::Value(format.c_str(),a),a);
    rapidjson::Value files(rapidjson::kArrayType);
    for(const auto& file:fs::recursive_directory_iterator(folder)) {
        if(!file.is_regular_file() || file.path().filename()=="template.json")continue;
        rapidjson::Value entry(rapidjson::kObjectType);
        const auto name=file.path().lexically_relative(folder).generic_string();
        entry.AddMember("path",rapidjson::Value(name.c_str(),a),a);
        entry.AddMember("size",static_cast<std::uint64_t>(fs::file_size(file.path())),a);files.PushBack(entry,a);
    }
    manifest.AddMember("files",files,a);
    write_json(folder/"template.json",manifest);
}
}
