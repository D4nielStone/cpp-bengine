#include "components/hud.hpp"
#include <bgui.hpp>
#include <cmath>
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <stdexcept>

namespace commons {
namespace {
rapidjson::Value write_vector(const bgui::vec3& value, rapidjson::Document::AllocatorType& a) {
    rapidjson::Value vector(rapidjson::kArrayType);
    vector.PushBack(value.x, a).PushBack(value.y, a).PushBack(value.z, a);
    return vector;
}

bool read_vector(const rapidjson::Value& value, bgui::vec3& result) {
    if (!value.IsArray() || value.Size() != 3) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!value[axis].IsNumber() || !std::isfinite(value[axis].GetFloat())) return false;
        result[axis] = value[axis].GetFloat();
    }
    return true;
}

bool read_legacy_text(const rapidjson::Value& item, hud_element::legacy_world_text& result) {
    if (!item.IsObject() || !item.HasMember("position") ||
        !read_vector(item["position"], result.position)) return false;
    if (item.HasMember("rotation") && !read_vector(item["rotation"], result.rotation)) return false;
    if (item.HasMember("world_scale") && !read_vector(item["world_scale"], result.scale)) return false;
    if (item.HasMember("depth")) {
        const auto& depth = item["depth"];
        if (!depth.IsNumber() || !std::isfinite(depth.GetFloat()) ||
            depth.GetFloat() < 0.f || depth.GetFloat() > 10000.f) return false;
        result.depth = depth.GetFloat();
    }
    return true;
}
}

bool hud_element::set_screen_position(const bgui::vec3& pixel) {
    constexpr float limit = std::numeric_limits<int>::max() / 2.f;
    const float unit = bgui::get_global_scale();
    if (!std::isfinite(unit) || unit <= 0.f || !std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
        std::abs(pixel.x) >= limit || std::abs(pixel.y) >= limit)
        return false;
    const bgui::vec3 layout_position{pixel.x / unit, pixel.y / unit, 0.f};
    if (!std::isfinite(layout_position.x) || !std::isfinite(layout_position.y)) return false;
    absolute = true;
    position = layout_position;
    return true;
}

std::shared_ptr<hud_element> hud::add(const std::string& kind, const std::string& text, int parent) {
    if (elements.size() >= 1024 || (kind != "column" && kind != "row" && kind != "text" && kind != "button"))
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
        v.AddMember("position", write_vector(e->position, a), a);
        if (const auto& legacy = e->legacy_text) {
            rapidjson::Value data(rapidjson::kObjectType);
            data.AddMember("position", write_vector(legacy->position, a), a);
            data.AddMember("rotation", write_vector(legacy->rotation, a), a);
            data.AddMember("world_scale", write_vector(legacy->scale, a), a);
            data.AddMember("depth", legacy->depth, a);
            v.AddMember("legacy_world_text", data, a);
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
        bgui::vec3 position;
        if (!item.HasMember("position") || !read_vector(item["position"], position)) return false;
        const std::string kind(item["kind"].GetString(), item["kind"].GetStringLength());
        const bool old_world_text = kind == "text3d";
        try {
            auto e = pending.add(old_world_text ? "text" : kind, item["text"].GetString(), item["parent"].GetInt());
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
            e->position = position;
            if (old_world_text) {
                if (!read_legacy_text(item, e->legacy_text.emplace())) return false;
                e->absolute = false;
                e->position = {0.f, 0.f, 0.f};
            } else if (item.HasMember("legacy_world_text")) {
                if (kind != "text" || !read_legacy_text(item["legacy_world_text"], e->legacy_text.emplace()))
                    return false;
            }
        } catch (const std::invalid_argument&) { return false; }
    }
    enabled=v["enabled"].GetBool(); elements=std::move(pending.elements); return true;
}
}
