#include <ranges>
#include "scene_graph.hpp"
#include "visual_lua.hpp"
#include "scene_files.hpp"
#include <map>
#include <set>

namespace editor::ui {
namespace fs = std::filesystem;
std::vector<fs::path> project_scripts(const fs::path& root) {
    std::vector<fs::path> result;
    for (auto it=fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied); it!=fs::recursive_directory_iterator(); ++it) {
        if(it->path().filename().string().starts_with('.') || fs::is_symlink(it->symlink_status())) { it.disable_recursion_pending(); continue; }
        auto ext=it->path().extension().string();
        std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
        if(ext==".lua" && it->is_regular_file()) result.push_back(it->path().lexically_normal());
    }
    std::sort(result.begin(),result.end()); return result;
}
scene_graph read_scene_graph(const fs::path& requested,
    const std::function<std::string(const fs::path&)>& read) {
    const auto root=fs::canonical(requested);
    auto contents=[&](const fs::path& file){ return read ? read(file) : project_files::read(file); };
    scene_graph graph;
    std::map<fs::path,std::size_t> index;
    for (const auto& scene : scene_files::list(root,true)) {
        index[scene.lexically_normal()]=graph.nodes.size();
        const auto n=graph.nodes.size();
        graph.nodes.push_back({scene,float(n%4)*280.f,float(n/4)*180.f,{}});
    }
    std::map<fs::path,std::vector<visual_lua::transition>> cache;
    for (std::size_t i=0;i<graph.nodes.size();++i) {
        auto& node=graph.nodes[i];
        try {
            rapidjson::Document doc;
            const auto source=contents(node.path); doc.Parse(source.data(),source.size());
            if(doc.HasParseError() || !doc.IsObject() || !doc.HasMember("entities") || !doc["entities"].IsArray()) throw std::runtime_error("Cena inválida: "+node.path.string());
            std::set<fs::path> scripts;
            auto visit=[&](auto&& self,const rapidjson::Value& v)->void {
                if(v.IsString()) {
                    const fs::path reference(std::string(v.GetString(),v.GetStringLength()));
                    if(reference.extension()==".lua") {
                        const auto file=fs::weakly_canonical(reference.is_absolute()?reference:root/reference);
                        if(scene_files::inside(file,root) && fs::is_regular_file(file)) scripts.insert(file);
                        else graph.diagnostics.push_back("Script indisponível ou externo: "+reference.string());
                    }
                } else if(v.IsArray()) { for(const auto& item:v.GetArray()) self(self,item); }
                else if(v.IsObject()) { for(const auto& item:std::ranges::subrange(v.MemberBegin(), v.MemberEnd())) {
                    const std::string key(item.name.GetString(),item.name.GetStringLength());
                    if(key=="directory" || key=="scripts") self(self,item.value);
                } }
            };
            for(const auto& entity:doc["entities"].GetArray())
                if(entity.IsObject() && entity.HasMember("components") && entity["components"].IsObject() && entity["components"].HasMember("code")) visit(visit,entity["components"]["code"]);
            node.scripts.assign(scripts.begin(),scripts.end());
            for(const auto& file:node.scripts) {
                if(!cache.contains(file)) cache[file]=visual_lua::transitions(contents(file));
                for(const auto& call:cache[file]) {
                    const auto target=call.reload?node.path:(root/fs::path(call.target)).lexically_normal();
                    const auto found=index.find(target);
                    const bool unresolved=call.dynamic || call.target.find('\0')!=std::string::npos || found==index.end() || (!call.reload && fs::path(call.target).is_absolute());
                    graph.edges.push_back({i,unresolved?i:found->second,file,call.reload?node.path.lexically_relative(root).generic_string():call.target,unresolved});
                }
            }
        } catch(const std::exception& error) { graph.diagnostics.push_back(error.what()); }
    }
    return graph;
}
}
