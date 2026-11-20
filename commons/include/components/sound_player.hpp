#pragma once

#include <string>

#include "component.hpp"

namespace COMMONS_NS {
    struct sound_player : component {
        static constexpr mask mask{COMPONENTE_SOUND_PLAYER};

        sound_player() = default;
        sound_player(const sound_player& other);
        sound_player& operator=(const sound_player& other);

        std::string clip;
        float volume{1.f};
        float pitch{1.f};
        float radius{10.f};
        bool looping{false};
        bool play_on_awake{false};

        enum class transport { automatic, playing, paused, stopped };
        bool play();
        bool pause();
        bool resume();
        bool stop();
        bool is_playing() const;
        transport playback_request() const { return m_transport; }
        std::uint64_t restart_revision() const { return m_restart; }

        bool analyze(const rapidjson::Value& value) override;
        bool serialize(
            rapidjson::Value& value,
            rapidjson::Document::AllocatorType& allocator
        ) const override;
    private:
        friend class sound_system;
        void report_playing(bool playing) { m_playing = playing; }
        bool live() const;
        transport m_transport{transport::automatic};
        std::uint64_t m_restart{};
        bool m_playing{};
    };
}
