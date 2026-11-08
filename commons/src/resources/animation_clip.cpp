#include "resources/animation_clip.hpp"
#include "os/resource_files.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

namespace commons {
std::optional<double> animation_track::sample(double time) const {
    if (keys.empty() || !std::isfinite(time)) return {};
    auto next = std::upper_bound(keys.begin(), keys.end(), time,
        [](double t, const auto& key) { return t < key.time; });
    if (next == keys.begin()) return next->value;
    const auto& previous = *std::prev(next);
    if (next == keys.end() || stepped) return previous.value;
    return std::lerp(previous.value, next->value, (time - previous.time) / (next->time - previous.time));
}
void animation_track::insert(animation_key key) {
    if (!std::isfinite(key.time) || key.time < 0 || !std::isfinite(key.value))
        throw std::invalid_argument("Invalid animation keyframe");
    auto it = std::lower_bound(keys.begin(), keys.end(), key.time,
        [](const auto& k, double t) { return k.time < t; });
    if (it != keys.end() && it->time == key.time) *it = key;
    else keys.insert(it, key);
}
bool animation_clip::analyze(const rapidjson::Value& value, bool legacy) {
    if (!legacy && (!value.IsObject() || !value.HasMember("format") || !value["format"].IsString() ||
        std::string_view(value["format"].GetString(),value["format"].GetStringLength()) != "bubble-animation-clip" || !value.HasMember("version") ||
        !value["version"].IsUint() || value["version"].GetUint() != 1)) return false;
    if (!value.IsObject() || !value.HasMember("duration") || !value["duration"].IsNumber() ||
        !std::isfinite(value["duration"].GetDouble()) || value["duration"].GetDouble() <= 0 ||
        value["duration"].GetDouble() > 86400 || !value.HasMember("loop") || !value["loop"].IsBool() ||
        !value.HasMember("tracks") || !value["tracks"].IsArray() || value["tracks"].Size() > 4096) return false;
    std::vector<animation_track> next;
    std::set<std::pair<unsigned, std::string>> identities;
    for (const auto& track : value["tracks"].GetArray()) {
        if (!track.IsObject() || !track.HasMember("component") || !track["component"].IsUint() ||
            !track.HasMember("property") || !track["property"].IsString() ||
            !track.HasMember("stepped") || !track["stepped"].IsBool() ||
            !track.HasMember("keys") || !track["keys"].IsArray() || track["keys"].Size() > 100000) return false;
        const auto target = track["component"].GetUint();
        std::string path(track["property"].GetString(), track["property"].GetStringLength());
        if (!target || (target & (target - 1)) || target == component::COMPONENTE_ANIMATION || path.empty() || path.size() > 1024 ||
            path.find('\0') != std::string::npos || !identities.emplace(target, path).second) return false;
        animation_track entry{static_cast<component::mask>(target), path, track["stepped"].GetBool(), {}};
        double previous = -1;
        for (const auto& key : track["keys"].GetArray()) {
            if (!key.IsArray() || key.Size() != 2 || !key[0].IsNumber() || !key[1].IsNumber()) return false;
            const double time = key[0].GetDouble(), number = key[1].GetDouble();
            if (!std::isfinite(time) || !std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max() ||
                time < 0 || time <= previous || time > value["duration"].GetDouble()) return false;
            entry.keys.push_back({time, number}); previous = time;
        }
        next.push_back(std::move(entry));
    }
    duration = value["duration"].GetDouble(); loop = value["loop"].GetBool();
    tracks = std::move(next);
    return true;
}
bool animation_clip::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& a) const {
    rapidjson::Value result(rapidjson::kObjectType), list(rapidjson::kArrayType);
    result.AddMember("format", "bubble-animation-clip", a).AddMember("version", 1, a);
    result.AddMember("duration", duration, a).AddMember("loop", loop, a);
    for (const auto& track : tracks) {
        rapidjson::Value item(rapidjson::kObjectType), keys(rapidjson::kArrayType);
        item.AddMember("component", static_cast<unsigned>(track.target), a);
        item.AddMember("property", rapidjson::Value(track.property.c_str(), a), a);
        item.AddMember("stepped", track.stepped, a);
        for (const auto& key : track.keys) {
            rapidjson::Value pair(rapidjson::kArrayType); pair.PushBack(key.time, a).PushBack(key.value, a);
            keys.PushBack(pair, a);
        }
        item.AddMember("keys", keys, a); list.PushBack(item, a);
    }
    result.AddMember("tracks", list, a);
    animation_clip checked;
    if (!checked.analyze(result)) return false;
    value = std::move(result);
    return true;
}
std::string animation_clip::encode() const {
    rapidjson::Document value(rapidjson::kObjectType);
    if (!serialize(value, value.GetAllocator())) throw std::runtime_error("Invalid animation clip data.");
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    if (!value.Accept(writer)) throw std::runtime_error("Could not encode animation clip.");
    return {buffer.GetString(), buffer.GetSize()};
}
std::filesystem::path animation_clip_path(const std::filesystem::path& root, const std::string& reference) {
    if (reference.empty() || reference.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid animation clip path.");
    const std::filesystem::path relative(reference);
    auto result = (relative.is_absolute() ? relative : root / relative).lexically_normal();
    if (result.extension() != ".banim") throw std::runtime_error("Animation clips must use .banim files.");
    if (!relative.is_absolute()) {
        const auto normalized = result.lexically_relative(root.lexically_normal());
        for (const auto& part : normalized)
            if (part == "..") throw std::runtime_error("Animation clip escapes the project directory.");
        if (!bgui::resource_files::embedded()) {
            const auto resolved = std::filesystem::weakly_canonical(result);
            const auto canonical_root = std::filesystem::weakly_canonical(root);
            for (const auto& part : resolved.lexically_relative(canonical_root))
                if (part == "..") throw std::runtime_error("Animation clip symlink escapes the project directory.");
        }
    }
    return bgui::resource_files::embedded() ? result : std::filesystem::weakly_canonical(result);
}
std::shared_ptr<animation_clip> load_animation_clip(const std::filesystem::path& path) {
    const auto contents = bgui::resource_files::text(path);
    if (contents.size() > 32 * 1024 * 1024) throw std::runtime_error("Animation clip exceeds 32 MiB.");
    rapidjson::Document document;
    document.Parse(contents.data(), contents.size());
    auto result = std::make_shared<animation_clip>();
    if (document.HasParseError() || !result->analyze(document))
        throw std::runtime_error("Invalid animation clip: " + path.string());
    result->source_path = path;
    result->source_contents = contents;
    result->saved_definition = result->encode();
    return result;
}
}
