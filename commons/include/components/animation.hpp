#pragma once
#include "component.hpp"
#include "resources/animation_clip.hpp"

namespace commons {
struct animation : component {
    static constexpr component::mask mask = COMPONENTE_ANIMATION;
    std::string clip;
    bool autoplay{true};
    double speed{1.};
    std::shared_ptr<animation_clip> data;
    animation() = default;
    animation(const animation& other);
    animation& operator=(const animation& other);
    bool play(const std::string& reference = "");
    bool pause();
    bool stop();
    bool seek(double seconds);
    bool cross_fade(const std::string& reference, double seconds);
    bool set_speed(double value);
    double playback_time() const { return m_time; }
    bool is_playing() const { return live() && m_playing; }
    bool is_blending() const { return live() && m_blending; }
    bool analyze(const rapidjson::Value&) override;
    bool serialize(rapidjson::Value&, rapidjson::Document::AllocatorType&) const override;
private:
    friend class animation_system;
    friend bool resolve_animation_clip(ecs&, animation&, const std::filesystem::path&, std::string&);
    enum class operation { play, pause, stop, seek, fade };
    struct command { operation op; double seconds{}; std::shared_ptr<animation_clip> asset; };
    std::vector<command> m_commands;
    std::filesystem::path m_project_root;
    double m_time{};
    bool m_playing{}, m_blending{};
    bool live() const;
    bool request(operation op, double seconds = 0, const std::string& reference = "");
};
// Resolves references once, sharing clips already loaded by other entities.
bool resolve_animation_clip(ecs& registry, animation& value, const std::filesystem::path& root, std::string& error);
}
