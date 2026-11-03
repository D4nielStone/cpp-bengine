#include "components/hud.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_set>
#include <stdexcept>

namespace commons {
std::shared_ptr<hud_element> hud::add(const std::string& kind, const std::string& text, int parent) {
    if (elements.size() >= 1024 || (kind != "column" && kind != "row" && kind != "text" && kind != "button" && kind != "text3d"))
        throw std::invalid_argument("Invalid HUD element type or limit exceeded");
    if (parent < -1 || parent >= static_cast<int>(elements.size()) ||
        (parent >= 0 && elements[parent]->kind != "row" && elements[parent]->kind != "column"))
        throw std::invalid_argument("HUD parent must be an existing row or column index");
    auto value = std::make_shared<hud_element>();
    value->kind = kind; value->text = text; value->parent = parent;
    for (unsigned n = 1;; ++n) {
        value->id = "widget_" + std::to_string(n);
        if (std::none_of(elements.begin(), elements.end(), [&](const auto& e) { return e->id == value->id; })) break;
    }
    elements.push_back(value);
    return value;
}
bool hud::serialize(rapidjson::Value& out, rapidjson::Document::AllocatorType& a) const {
    out.SetObject(); out.AddMember("enabled", enabled, a);
    rapidjson::Value list(rapidjson::kArrayType);
    for (const auto& e : elements) {
        rapidjson::Value v(rapidjson::kObjectType);
        v.AddMember("kind", rapidjson::Value(e->kind.c_str(), a), a);
        v.AddMember("text", rapidjson::Value(e->text.c_str(), a), a);
        v.AddMember("id", rapidjson::Value(e->id.c_str(), a), a);
        v.AddMember("callback", rapidjson::Value(e->callback.c_str(), a), a);
        v.AddMember("absolute", e->absolute, a);
        v.AddMember("padding", e->padding, a); v.AddMember("margin", e->margin, a);
        v.AddMember("alignment", e->alignment, a);
        rapidjson::Value color(rapidjson::kArrayType);
        for (int i=0;i<4;++i) color.PushBack(e->color[i],a);
        v.AddMember("color",color,a);
        v.AddMember("parent", e->parent, a);
        v.AddMember("width", e->width, a); v.AddMember("height", e->height, a);
        v.AddMember("scale", e->scale, a); v.AddMember("visible", e->visible, a);
        rapidjson::Value p(rapidjson::kArrayType);
        p.PushBack(e->position.x,a).PushBack(e->position.y,a).PushBack(e->position.z,a);
        v.AddMember("position", p,a);
        if (e->kind == "text3d") {
            rapidjson::Value rotation(rapidjson::kArrayType), scale(rapidjson::kArrayType);
            for (int axis = 0; axis < 3; ++axis) {
                rotation.PushBack(e->rotation[axis], a);
                scale.PushBack(e->world_scale[axis], a);
            }
            v.AddMember("rotation", rotation, a);
            v.AddMember("world_scale", scale, a);
            v.AddMember("depth", e->depth, a);
        }
        list.PushBack(v,a);
    }
    out.AddMember("elements",list,a);
    hud validated;
    return validated.analyze(out);
}
bool hud::analyze(const rapidjson::Value& v) {
    if (!v.IsObject() || !v.HasMember("enabled") || !v["enabled"].IsBool() || !v.HasMember("elements") || !v["elements"].IsArray()) return false;
    hud pending;
    std::unordered_set<std::string> ids;
    for (const auto& item : v["elements"].GetArray()) {
        if (!item.IsObject()) return false;
        for (auto key : {"kind", "text"}) if (!item.HasMember(key) || !item[key].IsString()) return false;
        if (!item.HasMember("parent") || !item["parent"].IsInt() || !item.HasMember("visible") || !item["visible"].IsBool()) return false;
        for (auto key : {"width", "height", "scale"}) if (!item.HasMember(key) || !item[key].IsNumber() || !std::isfinite(item[key].GetFloat()) || item[key].GetFloat() < 0 || item[key].GetFloat() > 10000) return false;
        if (!item.HasMember("position") || !item["position"].IsArray() || item["position"].Size()!=3) return false;
        for (const auto& n : item["position"].GetArray()) if (!n.IsNumber() || !std::isfinite(n.GetFloat())) return false;
        try {
            auto e = pending.add(item["kind"].GetString(),item["text"].GetString(),item["parent"].GetInt());
            if (item.HasMember("id")) {
                if (!item["id"].IsString() || item["id"].GetStringLength()==0) return false;
                e->id=item["id"].GetString();
            }
            if (!ids.insert(e->id).second) return false;
            if (item.HasMember("callback")) { if (!item["callback"].IsString()) return false; e->callback=item["callback"].GetString(); }
            if (item.HasMember("absolute")) { if (!item["absolute"].IsBool()) return false; e->absolute=item["absolute"].GetBool(); }
            for (auto key : {"padding", "margin"}) if(item.HasMember(key)) {
                if(!item[key].IsNumber() || !std::isfinite(item[key].GetFloat()) || item[key].GetFloat()<0 || item[key].GetFloat()>10000) return false;
                (std::string(key)=="padding" ? e->padding : e->margin)=item[key].GetFloat();
            }
            if(item.HasMember("alignment")) { if(!item["alignment"].IsInt() || item["alignment"].GetInt()<0 || item["alignment"].GetInt()>2) return false; e->alignment=item["alignment"].GetInt(); }
            if(item.HasMember("color")) {
                const auto& c=item["color"]; if(!c.IsArray() || c.Size()!=4) return false;
                for(int i=0;i<4;++i) { if(!c[i].IsNumber() || !std::isfinite(c[i].GetFloat()) || c[i].GetFloat()<0 || c[i].GetFloat()>1) return false; e->color[i]=c[i].GetFloat(); }
            }
            e->width=item["width"].GetFloat(); e->height=item["height"].GetFloat(); e->scale=item["scale"].GetFloat(); e->visible=item["visible"].GetBool();
            e->position={item["position"][0].GetFloat(),item["position"][1].GetFloat(),item["position"][2].GetFloat()};
            if (e->kind == "text3d") {
                const auto read_vector = [&](const char* key, bgui::vec3& result) {
                    if (!item.HasMember(key)) return true;
                    const auto& values = item[key];
                    if (!values.IsArray() || values.Size() != 3) return false;
                    for (int axis = 0; axis < 3; ++axis) {
                        if (!values[axis].IsNumber() || !std::isfinite(values[axis].GetFloat())) return false;
                        result[axis] = values[axis].GetFloat();
                    }
                    return true;
                };
                if (!read_vector("rotation", e->rotation) || !read_vector("world_scale", e->world_scale)) return false;
                if (item.HasMember("depth")) {
                    const auto& depth = item["depth"];
                    if (!depth.IsNumber() || !std::isfinite(depth.GetFloat()) ||
                        depth.GetFloat() < 0 || depth.GetFloat() > 10000) return false;
                    e->depth = depth.GetFloat();
                }
            }
        } catch (const std::invalid_argument&) { return false; }
    }
    enabled=v["enabled"].GetBool(); elements=std::move(pending.elements); return true;
}
}
