#include "components/sound_player.hpp"

#include <cmath>
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
