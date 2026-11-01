#pragma once

#include <string>

#include "component.hpp"

namespace COMMONS_NS {
    struct sound_player : component {
        static constexpr mask mask{COMPONENTE_SOUND_PLAYER};

        std::string clip;
        float volume{1.f};
        float pitch{1.f};
        float radius{10.f};
        bool looping{false};
        bool play_on_awake{false};

        bool analyze(const rapidjson::Value& value) override;
        bool serialize(
            rapidjson::Value& value,
            rapidjson::Document::AllocatorType& allocator
        ) const override;
    };
}
