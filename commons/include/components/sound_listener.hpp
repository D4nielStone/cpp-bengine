#pragma once

#include "component.hpp"

namespace COMMONS_NS {
    struct sound_listener : component {
        static constexpr mask mask{COMPONENTE_SOUND_LISTENER};

        bool enabled{true};

        bool analyze(const rapidjson::Value& value) override;
        bool serialize(
            rapidjson::Value& value,
            rapidjson::Document::AllocatorType& allocator
        ) const override;
    };
}
