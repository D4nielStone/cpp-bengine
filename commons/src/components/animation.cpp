#include "components/animation.hpp"
#include "core/ecs.hpp"
#include <cmath>

namespace commons {
animation::animation(const animation& other) { *this = other; }
animation& animation::operator=(const animation& other) {
    if (this == &other) return *this;
    component::operator=(other);
    clip = other.clip; autoplay = other.autoplay; speed = other.speed; data = other.data;
    m_project_root = other.m_project_root;
    m_commands.clear(); m_time = 0; m_playing = m_blending = false;
    return *this;
}
bool animation::live() const {
    auto owner = reg.lock();
    return owner && owner->get<animation>(my_object).get() == this;
}
bool animation::request(operation op, double seconds, const std::string& reference) {
    if (!live() || !std::isfinite(seconds) || seconds < 0 || m_commands.size() >= 64) return false;
    std::shared_ptr<animation_clip> asset;
    if (!reference.empty()) {
        if (m_project_root.empty() || std::filesystem::path(reference).is_absolute()) return false;
        animation loaded;
        loaded.clip = reference;
        std::string error;
        if (!resolve_animation_clip(*reg.lock(), loaded, m_project_root, error)) return false;
        asset = loaded.data;
    }
    if ((op == operation::play || op == operation::fade || op == operation::seek) && !asset && !data) return false;
    if (op == operation::fade && !asset) return false;
    m_commands.push_back({op, seconds, std::move(asset)});
    return true;
}
bool animation::play(const std::string& reference) { return request(operation::play, 0, reference); }
bool animation::pause() { return request(operation::pause); }
bool animation::stop() { return request(operation::stop); }
bool animation::seek(double seconds) { return request(operation::seek, seconds); }
bool animation::cross_fade(const std::string& reference, double seconds) { return request(operation::fade, seconds, reference); }
bool animation::set_speed(double value) {
    if (!live() || !std::isfinite(value) || value < 0 || value > 100) return false;
    speed = value; return true;
}
bool animation::analyze(const rapidjson::Value& value) {
    if (!value.IsObject()) return false;
    animation next;
    if (value.HasMember("autoplay")) {
        if (!value["autoplay"].IsBool()) return false;
        next.autoplay = value["autoplay"].GetBool();
    }
    if (value.HasMember("speed")) {
        if (!value["speed"].IsNumber() || !std::isfinite(value["speed"].GetDouble()) ||
            value["speed"].GetDouble() < 0 || value["speed"].GetDouble() > 100) return false;
        next.speed = value["speed"].GetDouble();
    }
    if (value.HasMember("clip")) {
        if (value.HasMember("tracks") || value.HasMember("duration")) return false;
        if (!value["clip"].IsString() || value["clip"].GetStringLength() > 4096) return false;
        next.clip.assign(value["clip"].GetString(), value["clip"].GetStringLength());
        if (next.clip.find('\0') != std::string::npos ||
            (!next.clip.empty() && std::filesystem::path(next.clip).extension() != ".banim")) return false;
    } else {
        next.data = std::make_shared<animation_clip>();
        if (!next.data->analyze(value, true)) return false;
    }
    clip = std::move(next.clip); autoplay = next.autoplay; speed = next.speed; data = std::move(next.data);
    m_commands.clear(); m_time = 0; m_playing = m_blending = false;
    return true;
}
bool animation::serialize(rapidjson::Value& value, rapidjson::Document::AllocatorType& a) const {
    if (!std::isfinite(speed) || speed < 0 || speed > 100 || clip.find('\0') != std::string::npos) return false;
    if (clip.empty() && data) {
        if (!data->serialize(value, a)) return false;
        value.AddMember("autoplay", autoplay, a).AddMember("speed", speed, a);
        return true; // Legacy in-memory clips are externalized by project persistence.
    }
    if (!clip.empty() && std::filesystem::path(clip).extension() != ".banim") return false;
    value.SetObject();
    value.AddMember("clip", rapidjson::Value(clip.c_str(), a), a);
    value.AddMember("autoplay", autoplay, a).AddMember("speed", speed, a);
    return true;
}
bool resolve_animation_clip(ecs& registry, animation& value, const std::filesystem::path& root, std::string& error) {
    value.m_project_root = root;
    if (value.clip.empty()) return true;
    try {
        const auto path = animation_clip_path(root, value.clip);
        if (value.data && value.data->source_path == path) return true;
        for (const auto& [id, components] : registry.entities) {
            auto it = components.find(animation::mask);
            if (it == components.end() || it->second.get() == &value) continue;
            const auto other = std::static_pointer_cast<animation>(it->second);
            if (other->data && other->data->source_path == path) { value.data = other->data; return true; }
        }
        value.data = load_animation_clip(path);
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}
}
