#pragma once
#include "components/animation.hpp"
#include "core/ecs.hpp"
#include "systems/system.hpp"
#include <functional>
#include <optional>

namespace commons {
std::vector<animation_property> animation_properties(component& value);
std::optional<double> read_animation_property(component& value, const std::string& path);
bool write_animation_property(component& value, const std::string& path, double number);
std::shared_ptr<component> animation_target(ecs& registry, uint32_t entity, component::mask mask);
void sample_animation(ecs& registry, uint32_t entity, const animation_clip& clip, double seconds);

class animation_system final : public system {
public:
    explicit animation_system(std::function<double()> delta) : m_delta(std::move(delta)) {}
    void setup(const std::shared_ptr<ecs>&) override {}
    void update(const std::shared_ptr<ecs>& registry) override;
private:
    struct blend_channel {
        component::mask mask;
        std::string path;
        std::weak_ptr<component> target;
        std::optional<std::size_t> source, destination;
        double fallback{};
        bool stepped{};
    };
    struct playback {
        std::weak_ptr<animation> clip;
        std::weak_ptr<animation_clip> asset;
        std::shared_ptr<animation_clip> current, source;
        std::vector<blend_channel> channels;
        double time{}, source_time{}, blend_time{}, blend_duration{};
        bool playing{}, manual{}, sample_requested{};
    };
    std::unordered_map<uint32_t, playback> m_playback;
    std::function<double()> m_delta;
};
}
