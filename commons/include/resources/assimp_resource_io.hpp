#pragma once
#include <assimp/IOSystem.hpp>
#include <assimp/IOStream.hpp>
#include <os/resource_files.hpp>
#include <cstring>
#include <limits>

namespace commons {
class resource_stream final : public Assimp::IOStream {
    bgui::resource_files::bytes data;
    size_t position{};
public:
    explicit resource_stream(bgui::resource_files::bytes value):data(std::move(value)) {}
    size_t Read(void* out,size_t size,size_t count) override {
        if(!size) return 0;
        count=std::min(count,(data.size()-position)/size);
        if (!count) return 0;
        std::memcpy(out,data.data()+position,count*size); position+=count*size; return count;
    }
    size_t Write(const void*,size_t,size_t) override {return 0;}
    aiReturn Seek(size_t offset,aiOrigin origin) override {
        size_t next=offset;
        if(origin==aiOrigin_CUR) {if(offset>data.size()-position)return aiReturn_FAILURE;next=position+offset;}
        if(origin==aiOrigin_END) {if(offset>data.size())return aiReturn_FAILURE;next=data.size()-offset;}
        if(next>data.size())return aiReturn_FAILURE; position=next;return aiReturn_SUCCESS;
    }
    size_t Tell() const override{return position;}
    size_t FileSize() const override{return data.size();}
    void Flush() override{}
};
class resource_io final : public Assimp::IOSystem {
public:
    bool Exists(const char* path) const override {return bgui::resource_files::exists(path);}
    char getOsSeparator() const override{return '/';}
    Assimp::IOStream* Open(const char* path,const char* mode="rb") override {
        if(mode[0]!='r' || !Exists(path))return nullptr;
        return new resource_stream(bgui::resource_files::read(path));
    }
    void Close(Assimp::IOStream* stream) override{delete stream;}
};
}
