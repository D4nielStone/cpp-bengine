#include "components/sound_player.hpp"

#include <cmath>
#include "core/ecs.hpp"
#include <utility>

using namespace COMMONS_NS;

bool sound_player::analyze(const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;

    if (value.HasMember("clip")) {
        if (!value["clip"].IsString() ||
            value["clip"].GetStringLength() !=
                std::char_traits<char>::length(value["clip"].GetString()))
            return false;
        clip = value["clip"].GetString();
    }
    for (const auto& [name, destination] : {
             std::pair{"volume", &volume},
             {"pitch", &pitch},
             {"radius", &radius}}) {
        if (!value.HasMember(name))
            continue;
        if (!value[name].IsNumber() || !std::isfinite(value[name].GetFloat()))
            return false;
        *destination = value[name].GetFloat();
    }
    if (value.HasMember("looping")) {
        if (!value["looping"].IsBool())
            return false;
        looping = value["looping"].GetBool();
    }
    if (value.HasMember("play_on_awake")) {
        if (!value["play_on_awake"].IsBool())
            return false;
        play_on_awake = value["play_on_awake"].GetBool();
    }
    return volume >= 0.f && volume <= 1.f &&
        pitch >= 0.1f && pitch <= 3.f &&
        radius >= 0.1f && radius <= 10000.f;
}

bool sound_player::serialize(
    rapidjson::Value& value,
    rapidjson::Document::AllocatorType& allocator
) const {
    if (!value.IsObject() || clip.find('\0') != std::string::npos ||
        !std::isfinite(volume) || !std::isfinite(pitch) ||
        !std::isfinite(radius) || volume < 0.f || volume > 1.f ||
        pitch < 0.1f || pitch > 3.f || radius < 0.1f || radius > 10000.f)
        return false;

    rapidjson::Value clip_value;
    clip_value.SetString(clip.c_str(), static_cast<rapidjson::SizeType>(clip.size()), allocator);
    value.AddMember("clip", clip_value, allocator);
    value.AddMember("volume", volume, allocator);
    value.AddMember("pitch", pitch, allocator);
    value.AddMember("radius", radius, allocator);
    value.AddMember("looping", looping, allocator);
    value.AddMember("play_on_awake", play_on_awake, allocator);
    return true;
}

bool sound_player::live() const {
    auto owner = reg.lock();
    return owner && owner->get<sound_player>(my_object).get() == this;
}
bool sound_player::play() {
    if (!live() || clip.empty()) return false;
    m_transport = transport::playing;
    ++m_restart;
    return true;
}
bool sound_player::pause() {
    if (!live()) return false;
    m_transport = transport::paused;
    return true;
}
bool sound_player::resume() {
    if (!live() || clip.empty()) return false;
    m_transport = transport::playing;
    return true;
}
bool sound_player::stop() {
    if (!live()) return false;
    m_transport = transport::stopped;
    return true;
}
bool sound_player::is_playing() const { return live() && m_playing; }

sound_player::sound_player(const sound_player& other) : component(other) { *this = other; }
sound_player& sound_player::operator=(const sound_player& other) {
    if (this == &other) return *this;
    clip = other.clip;
    volume = other.volume;
    pitch = other.pitch;
    radius = other.radius;
    looping = other.looping;
    play_on_awake = other.play_on_awake;
    m_transport = transport::automatic;
    m_restart = 0;
    m_playing = false;
    return *this;
}
