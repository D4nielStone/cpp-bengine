#include "loaders/material_asset.hpp"
#include "util/material.hpp"
#include <os/resource_files.hpp>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utils/texture.hpp>
#include <zlib.h>

namespace commons {
namespace {
using value = rapidjson::Value;
using allocator = rapidjson::Document::AllocatorType;
value string(const std::string& text, allocator& a) { return value(text.data(),static_cast<rapidjson::SizeType>(text.size()),a); }
value numbers(const float* values, unsigned count, allocator& a) {
    value array(rapidjson::kArrayType);
    for (unsigned i=0;i<count;++i) {
        if (!std::isfinite(values[i])) throw std::runtime_error("Material contains a non-finite number.");
        array.PushBack(values[i],a);
    }
    return array;
}
std::string path_string(const std::string& path, const std::filesystem::path& directory) {
    if (path.empty()) return {};
    auto result=std::filesystem::path(path);
    if (result.is_absolute()) {
        const auto relative=result.lexically_relative(std::filesystem::absolute(directory));
        if (!relative.empty()) result=relative;
    }
    return result.generic_string();
}
bool matches_file(const std::filesystem::path& path, std::string_view contents) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char block[8192];
    std::size_t offset = 0;
    while (input) {
        input.read(block, sizeof(block));
        const auto count = static_cast<std::size_t>(input.gcount());
        if (count > contents.size() - offset || std::memcmp(block, contents.data() + offset, count)) return false;
        offset += count;
    }
    return input.eof() && offset == contents.size();
}
bool valid_string(const value& v) { return v.IsString() && v.GetStringLength()==std::strlen(v.GetString()); }
void require(bool condition) { if (!condition) throw std::runtime_error("Malformed .bmat material."); }
void read_numbers(const value& v, unsigned count, float* out) {
    require(v.IsArray() && v.Size()==count);
    for (unsigned i=0;i<count;++i) { require(v[i].IsNumber() && std::isfinite(v[i].GetFloat())); out[i]=v[i].GetFloat(); }
}
std::filesystem::path content_path(const std::filesystem::path& directory,
    std::string_view contents, std::string_view extension) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : contents) { hash ^= byte; hash *= 1099511628211ull; }
    std::ostringstream name;
    name << std::hex << std::setfill('0') << std::setw(16) << hash;
    const auto stem = name.str();
    auto path = directory / (stem + std::string(extension));
    for (unsigned suffix = 2; std::filesystem::exists(path); ++suffix) {
        if (matches_file(path, contents)) return path;
        path = directory / (stem + "-" + std::to_string(suffix) + std::string(extension));
    }
    return path;
}

void publish_asset(const std::filesystem::path& path, std::string_view contents) {
    if (std::filesystem::exists(path)) {
        if (!matches_file(path, contents))
            throw std::runtime_error("Existing asset differs; preserved: " + path.string());
        return;
    }
    const auto directory = path.parent_path();
    std::filesystem::create_directories(directory);
    static std::atomic_uint64_t next{0};
    std::filesystem::path staging;
    while (staging.empty()) {
        const auto candidate = directory / (".bmat-save-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(next.fetch_add(1)));
        if (std::filesystem::create_directory(candidate)) staging = candidate;
    }
    struct cleanup {
        const std::filesystem::path& staging;
        ~cleanup() { std::error_code ignored; std::filesystem::remove_all(staging, ignored); }
    } guard{staging};
    const auto temp = staging / "asset";
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), contents.size());
        output.close();
        if (!output) throw std::runtime_error("Could not write asset: " + path.string());
    }
    std::error_code ec;
    std::filesystem::create_hard_link(temp, path, ec);
    if (ec && !matches_file(path, contents))
        throw std::filesystem::filesystem_error("Could not publish asset", path, ec);
}

std::string png_from_pixels(const bgui::texture& image, unsigned width, unsigned height) {
    const auto row_size = std::size_t(width) * 4;
    std::string rows((row_size + 1) * height, '\0');
    for (unsigned row = 0; row < height; ++row)
        std::memcpy(rows.data() + row * (row_size + 1) + 1,
            image.m_buffer.data() + row * row_size, row_size);

    auto size = compressBound(rows.size());
    std::string compressed(size, '\0');
    if (compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
        reinterpret_cast<const Bytef*>(rows.data()), rows.size(), Z_BEST_SPEED) != Z_OK)
        throw std::runtime_error("Could not encode material texture as PNG.");
    compressed.resize(size);

    const auto append_number = [](std::string& bytes, std::uint32_t number) {
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes.push_back(static_cast<char>((number >> shift) & 0xff));
    };
    std::string png("\x89PNG\r\n\x1a\n", 8);
    const auto append_chunk = [&](std::string_view type, std::string_view bytes) {
        append_number(png, static_cast<std::uint32_t>(bytes.size()));
        const auto start = png.size();
        png.append(type);
        png.append(bytes);
        append_number(png, crc32(0, reinterpret_cast<const Bytef*>(png.data() + start),
            static_cast<uInt>(type.size() + bytes.size())));
    };
    std::string header;
    append_number(header, width);
    append_number(header, height);
    header.append("\x08\x06\0\0\0", 5);
    append_chunk("IHDR", header);
    append_chunk("IDAT", compressed);
    append_chunk("IEND", {});
    return png;
}

std::string encode_material(const material& source, const std::filesystem::path& directory,
    bool publish_textures) {
    rapidjson::Document doc(rapidjson::kObjectType); auto& a=doc.GetAllocator();
    doc.AddMember("format","bengine-material",a); doc.AddMember("version",1,a);
    value uniforms(rapidjson::kObjectType);
    std::map<std::string,uniform> ordered(source.uniforms.begin(),source.uniforms.end());
    for (const auto& [name,u] : ordered) {
        if (name=="instance" || name.starts_with("transformacoes[")) continue;
        value entry(rapidjson::kObjectType), data;
        auto type=u.m_type;
        int tag=static_cast<int>(type);
        if (tag>=static_cast<int>(uniform::type::Float_ptr)) tag-=static_cast<int>(uniform::type::Float_ptr);
        float v[16]{}; unsigned count=0;
        switch (type) {
        case uniform::type::Float: v[0]=u.valor.f; count=1; break;
        case uniform::type::Float_ptr: require(u.ponteiro.f_ptr); v[0]=*u.ponteiro.f_ptr; count=1; break;
        case uniform::type::Int: data.SetInt(u.valor.i); break;
        case uniform::type::Int_ptr: require(u.ponteiro.i_ptr); data.SetInt(*u.ponteiro.i_ptr); break;
        case uniform::type::Bool: data.SetBool(u.valor.b); break;
        case uniform::type::Bool_ptr: require(u.ponteiro.b_ptr); data.SetBool(*u.ponteiro.b_ptr); break;
        case uniform::type::Vet2: v[0]=u.valor.vec2.x;v[1]=u.valor.vec2.y;count=2;break;
        case uniform::type::Vet2_ptr: require(u.ponteiro.vec2_ptr);v[0]=u.ponteiro.vec2_ptr->x;v[1]=u.ponteiro.vec2_ptr->y;count=2;break;
        case uniform::type::Vet3: for(int i=0;i<3;++i)v[i]=u.valor.vec3[i];count=3;break;
        case uniform::type::Vet3_ptr: require(u.ponteiro.vec3_ptr);for(int i=0;i<3;++i)v[i]=(*u.ponteiro.vec3_ptr)[i];count=3;break;
        case uniform::type::Vet4: for(int i=0;i<4;++i)v[i]=u.valor.vec4[i];count=4;break;
        case uniform::type::Vet4_ptr: require(u.ponteiro.vec4_ptr);for(int i=0;i<4;++i)v[i]=(*u.ponteiro.vec4_ptr)[i];count=4;break;
        case uniform::type::Cor: v[0]=u.valor.color_value.r;v[1]=u.valor.color_value.g;v[2]=u.valor.color_value.b;v[3]=u.valor.color_value.a;count=4;break;
        case uniform::type::Cor_ptr: require(u.ponteiro.color_ptr);v[0]=u.ponteiro.color_ptr->r;v[1]=u.ponteiro.color_ptr->g;v[2]=u.ponteiro.color_ptr->b;v[3]=u.ponteiro.color_ptr->a;count=4;break;
        case uniform::type::Mat4: std::copy_n(glm::value_ptr(u.valor.mat4),16,v);count=16;break;
        case uniform::type::Mat4_ptr: require(u.ponteiro.mat4_ptr);std::copy_n(glm::value_ptr(*u.ponteiro.mat4_ptr),16,v);count=16;break;
        }
        if (count) data=numbers(v,count,a);
        entry.AddMember("type",tag,a); entry.AddMember("value",data,a);
        uniforms.AddMember(string(name,a),entry,a);
    }
    doc.AddMember("uniforms",uniforms,a);
    value textures(rapidjson::kObjectType);
    std::map<std::string,texture> maps(source.textures.begin(),source.textures.end());
    for (const auto& [name,t] : maps) {
        value entry(rapidjson::kObjectType);
        require(static_cast<int>(t.filtering)>=0 && static_cast<int>(t.filtering)<=2);
        auto reference = t.embedded ? std::string{} : path_string(t.path, directory);
        entry.AddMember("filtering",static_cast<int>(t.filtering),a);
        if (t.embedded) {
            const auto& image=*t.embedded;
            require(std::isfinite(image.m_size.x) && std::isfinite(image.m_size.y) &&
                image.m_size.x >= 1 && image.m_size.y >= 1 &&
                image.m_size.x <= 16'777'216 && image.m_size.y <= 16'777'216);
            const auto width=static_cast<int>(image.m_size.x), height=static_cast<int>(image.m_size.y);
            require(width>0 && height>0 && static_cast<std::uint64_t>(width)*height<=16'777'216 && image.m_buffer.size()==static_cast<std::size_t>(width)*height*4);
            std::string generated;
            std::string_view bytes;
            if (t.imported_image && !t.imported_image->empty() && t.imported_image_revision == image.m_revision)
                bytes = {reinterpret_cast<const char*>(t.imported_image->data()), t.imported_image->size()};
            std::string_view extension = ".png";
            if (bytes.starts_with(std::string_view("\xff\xd8", 2))) {
                extension = ".jpg";
            } else if (bytes.empty()) {
                generated = png_from_pixels(image, width, height);
                bytes = generated;
            } else if (!bytes.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8))) {
                extension = ".img";
            }
            const auto file = content_path(directory, bytes, extension);
            if (publish_textures) publish_asset(file, bytes);
            reference = file.filename().generic_string();
        } else if (t.id && t.path.empty()) throw std::runtime_error("GPU-only material texture has no recoverable source.");
        entry.AddMember("path", string(reference, a), a);
        textures.AddMember(string(name,a),entry,a);
    }
    doc.AddMember("textures",textures,a);
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!doc.Accept(writer)) throw std::runtime_error("Could not encode material.");
    require(buffer.GetSize()<=256u*1024u*1024u);
    return {buffer.GetString(),buffer.GetSize()};
}
}

std::string encode_material_asset(const material& source, const std::filesystem::path& directory) {
    return encode_material(source, directory, false);
}

std::filesystem::path material_asset_path(const std::filesystem::path& directory, const std::string& contents) {
    return content_path(directory, contents, ".bmat");
}

bool store_material_asset(const std::filesystem::path& directory, const material& source, std::filesystem::path& result, std::string& error) {
    try {
        const auto contents = encode_material(source, directory, true);
        const auto path = material_asset_path(directory, contents);
        publish_asset(path, contents);
        result = path;
        error.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool load_material_asset(const std::filesystem::path& path, material& result, std::string& error) {
    try {
        auto input=bgui::resource_files::stream(path);require(bool(input));
        std::string text;char block[8192];
        while (input) { input.read(block,sizeof(block));text.append(block,static_cast<std::size_t>(input.gcount()));require(text.size()<=256u*1024u*1024u); }
        require(input.eof());rapidjson::Document doc;doc.Parse(text.data(),text.size());
        require(!doc.HasParseError() && doc.IsObject() && doc.HasMember("format") && valid_string(doc["format"]) && std::string_view(doc["format"].GetString())=="bengine-material" && doc.HasMember("version") && doc["version"].IsInt());
        require(doc["version"].GetInt() == 1);
        require(doc.HasMember("uniforms") && doc["uniforms"].IsObject() && doc.HasMember("textures") && doc["textures"].IsObject());
        material loaded;
        for (auto it=doc["uniforms"].MemberBegin();it!=doc["uniforms"].MemberEnd();++it) {
            const auto& entry=it->value; require(valid_string(it->name) && entry.IsObject() && entry.HasMember("type") && entry["type"].IsInt() && entry.HasMember("value"));
            const auto& data=entry["value"];float n[16]{};uniform u;
            switch (entry["type"].GetInt()) {
            case 0: read_numbers(data,1,n);u=uniform(n[0]);break;
            case 1: require(data.IsInt());u=uniform(data.GetInt());break;
            case 2: require(data.IsBool());u=uniform(data.GetBool());break;
            case 3: read_numbers(data,2,n);u=uniform(bgui::vec2{n[0],n[1]});break;
            case 4: read_numbers(data,3,n);u=uniform(bgui::vec3{n[0],n[1],n[2]});break;
            case 5: read_numbers(data,4,n);u=uniform(bgui::vec4{n[0],n[1],n[2],n[3]});break;
            case 6: read_numbers(data,4,n);u=uniform(color{n[0],n[1],n[2],n[3]});break;
            case 7: read_numbers(data,16,n);u=uniform(glm::make_mat4(n));break;
            default: require(false);
            }
            loaded.set_uniform(it->name.GetString(),u);
        }
        for (auto it=doc["textures"].MemberBegin();it!=doc["textures"].MemberEnd();++it) {
            const auto& entry=it->value;require(valid_string(it->name) && entry.IsObject() && entry.HasMember("path") && valid_string(entry["path"]) && entry.HasMember("filtering") && entry["filtering"].IsInt());
            const int filtering=entry["filtering"].GetInt();require(filtering>=0 && filtering<=2);
            const std::filesystem::path reference(entry["path"].GetString());
            texture t{0,reference.empty()?std::string{}:(reference.is_absolute()?reference:path.parent_path()/reference).lexically_normal().string()};t.filtering=static_cast<texture_filter>(filtering);
            const bool legacy_pixels = entry.HasMember("rgba");
            if (legacy_pixels) {
                require(entry.HasMember("width") && entry["width"].IsUint() &&
                    entry.HasMember("height") && entry["height"].IsUint());
                const auto w = entry["width"].GetUint(), h = entry["height"].GetUint();
                require(w && h && std::uint64_t(w) * h <= 16'777'216);
                const auto size = std::size_t(w) * h * 4;
                require(valid_string(entry["rgba"]) && entry["rgba"].GetStringLength() == size * 2);
                auto image = std::make_shared<bgui::texture>();
                image->m_size = {float(w), float(h)};
                image->m_has_alpha = true;
                image->m_buffer.resize(size);
                const auto digit = [](char c) -> unsigned {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    throw std::runtime_error("Invalid embedded texture bytes.");
                };
                const char* bytes = entry["rgba"].GetString();
                for (std::size_t i = 0; i < size; ++i)
                    image->m_buffer[i] = (digit(bytes[2*i]) << 4) | digit(bytes[2*i+1]);
                t.path = path.string() + "#" + it->name.GetString();
                image->m_path = t.path;
                t.embedded = std::move(image);
                t.update_embedded_revision();
            }
            loaded.set_texture(it->name.GetString(),t);
        }
        result=std::move(loaded);error.clear();return true;
    } catch (const std::exception& e) { error="Could not load material '"+path.string()+"': "+e.what();return false; }
}
bool material_asset_dependencies(const std::filesystem::path& path, std::vector<std::filesystem::path>& result, std::string& error) {
    material value;
    if (!load_material_asset(path, value, error)) return false;
    std::vector<std::filesystem::path> loaded;
    for (const auto& [name, texture] : value.textures) {
        (void)name;
        if (!texture.embedded && !texture.path.empty()) loaded.emplace_back(texture.path);
    }
    result = std::move(loaded);
    return true;
}

}
