#include "components/sound_listener.hpp"

using namespace COMMONS_NS;

bool sound_listener::analyze(const rapidjson::Value& value) {
    if (!value.IsObject())
        return false;
    if (value.HasMember("enabled")) {
        if (!value["enabled"].IsBool())
            return false;
        enabled = value["enabled"].GetBool();
    }
    return true;
}

bool sound_listener::serialize(
    rapidjson::Value& value,
    rapidjson::Document::AllocatorType& allocator
) const {
    if (!value.IsObject())
        return false;
    value.AddMember("enabled", enabled, allocator);
    return true;
}
