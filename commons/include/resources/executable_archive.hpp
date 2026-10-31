#pragma once
#include <zlib.h>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace executable_archive {
namespace fs = std::filesystem;
inline volatile char required_marker[] = "BENGINE_EMBED_REQUIRED_V1:0";
inline bool required() { return required_marker[sizeof(required_marker)-2] == '1'; }
inline constexpr char magic[] = "BENGINE_PACK_V1!";
inline constexpr std::uint64_t footer_size = 36, file_limit = 512ull*1024*1024;
using bytes = std::vector<unsigned char>;
inline std::uint64_t read_number(std::istream& in, unsigned width) {
    std::uint64_t value=0;
    for (unsigned i=0;i<width;++i) {
        const int c=in.get(); if(c==EOF) throw std::runtime_error("Truncated executable package.");
        value |= std::uint64_t(static_cast<unsigned char>(c)) << (i*8);
    }
    return value;
}
inline void write_number(std::ostream& out, std::uint64_t value, unsigned width) {
    for (unsigned i=0;i<width;++i) out.put(static_cast<char>((value >> (i*8)) & 255));
}
inline bool safe_name(const std::string& name) {
    const fs::path path(name);
    if(name.empty() || path.is_absolute() || name.find('\\')!=std::string::npos || name.find(':')!=std::string::npos || name.find('\0')!=std::string::npos) return false;
    for(const auto& part:path) if(part==".." || part==".") return false;
    return path.generic_string()==name;
}
inline bytes read_file(const fs::path& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Could not open archive input: "+path.string());
    const auto size=fs::file_size(path);
    if(size>file_limit) throw std::runtime_error("Package file exceeds 512 MiB: "+path.string());
    bytes data(static_cast<std::size_t>(size)); in.read(reinterpret_cast<char*>(data.data()),data.size());
    if(!in) throw std::runtime_error("Could not read archive input."); return data;
}
struct entry { std::uint64_t offset, packed, size; std::uint32_t crc; };
class reader {
    fs::path executable;
    std::map<std::string,entry> entries;
public:
    static std::shared_ptr<reader> open(const fs::path& file) {
        std::ifstream in(file,std::ios::binary|std::ios::ate);
        if(!in) throw std::runtime_error("Could not open executable package.");
        const auto end=static_cast<std::uint64_t>(in.tellg());
        if(end<footer_size) return {};
        in.seekg(end-footer_size);
        std::array<char,16> signature{}; in.read(signature.data(),signature.size());
        if(std::string(signature.data(),signature.size())!=std::string(magic,16)) return {};
        const auto offset=read_number(in,8), size=read_number(in,8), checksum=read_number(in,4);
        if(size>16*1024*1024 || offset>end-footer_size || size!=end-footer_size-offset)
            throw std::runtime_error("Invalid executable package index range.");
        in.seekg(offset); std::string index(static_cast<std::size_t>(size),'\0'); in.read(index.data(),index.size());
        if(!in || crc32(0,reinterpret_cast<const Bytef*>(index.data()),index.size())!=checksum)
            throw std::runtime_error("Corrupted executable package index.");
        rapidjson::Document document; document.Parse(index.data(),index.size());
        if(document.HasParseError() || !document.IsArray() || document.Size()>100000)
            throw std::runtime_error("Invalid executable package index.");
        auto result=std::make_shared<reader>(); result->executable=file;
        for(const auto& value:document.GetArray()) {
            if(!value.IsObject() || !value.HasMember("path") || !value["path"].IsString())
                throw std::runtime_error("Invalid executable package entry.");
            for(const auto* key:{"offset","packed","size","crc"})
                if(!value.HasMember(key) || !value[key].IsUint64()) throw std::runtime_error("Invalid package sizes.");
            const std::string path(value["path"].GetString(),value["path"].GetStringLength());
            entry item{value["offset"].GetUint64(),value["packed"].GetUint64(),value["size"].GetUint64(),0};
            if(!safe_name(path) || item.size>file_limit || item.packed>file_limit || item.offset>offset || item.packed>offset-item.offset || value["crc"].GetUint64()>UINT32_MAX)
                throw std::runtime_error("Unsafe executable package entry.");
            item.crc=static_cast<std::uint32_t>(value["crc"].GetUint64());
            if(!result->entries.emplace(path,item).second) throw std::runtime_error("Duplicate package entry.");
        }
        return result;
    }
    bool contains(const std::string& name) const { return entries.contains(name); }
    bytes read(const std::string& name) const {
        const auto it=entries.find(name);
        if(it==entries.end()) throw std::runtime_error("Missing embedded resource: "+name);
        const auto& e=it->second;
        std::ifstream in(executable,std::ios::binary); in.seekg(e.offset);
        bytes compressed(static_cast<std::size_t>(e.packed)); in.read(reinterpret_cast<char*>(compressed.data()),compressed.size());
        bytes data(static_cast<std::size_t>(std::max<std::uint64_t>(e.size,1)));
        uLongf size=data.size();
        if(!in || uncompress(data.data(),&size,compressed.data(),compressed.size())!=Z_OK || size!=e.size || crc32(0,data.data(),size)!=e.crc)
            throw std::runtime_error("Corrupted embedded resource: "+name);
        data.resize(static_cast<std::size_t>(e.size)); return data;
    }
};
inline void append(const fs::path& binary,const fs::path& resources,const std::string& binary_name,const fs::path& output) {
    auto prefix=read_file(binary);
    const std::string marker="BENGINE_EMBED_REQUIRED_V1:0";
    const auto found=std::search(prefix.begin(),prefix.end(),marker.begin(),marker.end());
    if(found==prefix.end()) throw std::runtime_error("Runtime template does not support embedded packages.");
    *(found+marker.size()-1)='1';
    fs::create_directories(output.parent_path());
    std::ofstream out(output,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<char*>(prefix.data()),prefix.size());
    rapidjson::Document index(rapidjson::kArrayType); auto& allocator=index.GetAllocator();
    std::vector<fs::path> paths;
    for(const auto& file:fs::recursive_directory_iterator(resources)) {
        if(!file.is_regular_file()) continue;
        const auto relative=file.path().lexically_relative(resources).generic_string();
        if(relative==binary_name) continue;
        if(!safe_name(relative)) throw std::runtime_error("Unsafe package filename.");
        paths.push_back(file.path());
    }
    std::sort(paths.begin(),paths.end());
    for(const auto& path:paths) {
        const auto data=read_file(path); uLongf size=compressBound(data.size()); bytes compressed(size);
        if(compress2(compressed.data(),&size,data.data(),data.size(),Z_BEST_COMPRESSION)!=Z_OK) throw std::runtime_error("Package compression failed.");
        rapidjson::Value entry(rapidjson::kObjectType);
        const auto name=path.lexically_relative(resources).generic_string();
        entry.AddMember("path",rapidjson::Value(name.c_str(),allocator),allocator);
        entry.AddMember("offset",static_cast<std::uint64_t>(out.tellp()),allocator);
        entry.AddMember("packed",static_cast<std::uint64_t>(size),allocator);
        entry.AddMember("size",static_cast<std::uint64_t>(data.size()),allocator);
        entry.AddMember("crc",static_cast<std::uint64_t>(crc32(0,data.data(),data.size())),allocator);
        index.PushBack(entry,allocator); out.write(reinterpret_cast<char*>(compressed.data()),size);
    }
    const auto offset=static_cast<std::uint64_t>(out.tellp());
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer); index.Accept(writer);
    out.write(buffer.GetString(),buffer.GetSize()); out.write(magic,16);
    write_number(out,offset,8); write_number(out,buffer.GetSize(),8);
    write_number(out,crc32(0,reinterpret_cast<const Bytef*>(buffer.GetString()),buffer.GetSize()),4);
    out.close(); if(!out) throw std::runtime_error("Could not write single executable.");
    fs::permissions(output,fs::status(binary).permissions());
}
}
