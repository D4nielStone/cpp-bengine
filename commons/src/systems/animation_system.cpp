#include "systems/rig_entities.hpp"
#include "systems/animation_system.hpp"
#include "components/transform.hpp"
#include "components/camera.hpp"
#include "components/renderer.hpp"
#include "components/directional_light.hpp"
#include "components/point_light.hpp"
#include "components/spot_light.hpp"
#include "components/physics.hpp"
#include "components/sound_player.hpp"
#include "components/sound_listener.hpp"
#include "components/hud.hpp"
#include "components/code.hpp"
#include "components/properties.hpp"
#include "components/text.hpp"

namespace commons {
namespace {
template<class Visit> void channels(component& c, Visit visit) {
    auto scalar = [&](const std::string& path, auto& value,
        double low = -std::numeric_limits<float>::max(), double high = std::numeric_limits<float>::max()) {
        using type = std::remove_cvref_t<decltype(value)>;
        constexpr bool discrete = std::is_integral_v<type>;
        if constexpr (std::is_same_v<type, bool>) { low = 0; high = 1; }
        visit(animation_property{path, low, high, discrete}, static_cast<double>(value),
            [&](double number) { value = static_cast<type>(number); });
    };
    auto vector = [&](const std::string& name, auto& value, int count = 3, double low = -std::numeric_limits<float>::max(), double high = std::numeric_limits<float>::max()) {
        constexpr const char* axes[]{"x", "y", "z", "w"};
        for (int i = 0; i < count; ++i) scalar(name + "." + axes[i], value[i], low, high);
    };
    if (auto* v = dynamic_cast<transform*>(&c)) {
        vector("position", v->position); vector("rotation", v->rotation); vector("scale", v->scale);
    } else if (auto* v = dynamic_cast<camera*>(&c)) {
        scalar("fov", v->config.fov, 1, 179); scalar("scale", v->config.scale, .001, 100000);
        scalar("fog_density", v->config.fog_density, 0, 100); scalar("fog_enabled", v->config.fog_enabled);
        scalar("orthographic", v->config.flag_orth); scalar("fog_color.r", v->config.fog_color.r, 0, 1);
        scalar("fog_color.g", v->config.fog_color.g, 0, 1); scalar("fog_color.b", v->config.fog_color.b, 0, 1);
        scalar("occlusion.radius", v->config.post_processing.radius, .05, 10);
        scalar("occlusion.strength", v->config.post_processing.strength, 0, 1);
    } else if (auto* v = dynamic_cast<renderer*>(&c)) {
        scalar("visible", v->visible); scalar("cast_shadows", v->cast_shadows); scalar("receive_shadows", v->receive_shadows);
    } else if (auto* v = dynamic_cast<directional_light*>(&c)) {
        vector("direction", v->direction); vector("ambient", v->ambient, 3, 0, 1); vector("color", v->color, 3, 0, 1);
        scalar("intensity", v->intensity, 0, 100000); scalar("shadows", v->shadows);
        scalar("shadow_distance", v->shadow_distance, .01, 10000); scalar("shadow_bias", v->shadow_bias, 0, 1);
        scalar("shadow_strength", v->shadow_strength, 0, 1);
    } else if (auto* v = dynamic_cast<spot_light*>(&c)) {
        vector("position", v->position); vector("direction", v->direction); vector("color", v->color, 3, 0, 1);
        scalar("intensity", v->intensity, 0, 100000); scalar("shadows", v->shadows);
        scalar("constant", v->constant, .001, 10000); scalar("linear", v->linear, 0, 10000);
        scalar("quadratic", v->quadratic, 0, 10000); scalar("shadow_strength", v->shadow_strength, 0, 1);
    } else if (auto* v = dynamic_cast<point_light*>(&c)) {
        vector("position", v->position); vector("color", v->color, 3, 0, 1); scalar("intensity", v->intensity, 0, 100000);
    } else if (auto* v = dynamic_cast<physics*>(&c)) {
        // Physics setters update the live body as well as serialized values.
        visit(animation_property{"mass", 0, 100000}, double(v->m_massa), [&](double n) { v->setMass(float(n)); });
        visit(animation_property{"friction", 0, 10}, double(v->friction), [&](double n) { v->setFriccao(float(n)); });
        visit(animation_property{"restitution", 0, 1}, double(v->restitution), [&](double n) { v->setRestituicao(float(n)); });
    } else if (auto* v = dynamic_cast<sound_player*>(&c)) {
        scalar("volume", v->volume, 0, 1); scalar("pitch", v->pitch, .1, 3); scalar("radius", v->radius, .1, 10000);
        scalar("looping", v->looping); scalar("play_on_awake", v->play_on_awake);
    } else if (auto* v = dynamic_cast<sound_listener*>(&c)) {
        scalar("enabled", v->enabled);
    } else if (auto* v = dynamic_cast<hud*>(&c)) {
        scalar("enabled", v->enabled);
        for (std::size_t i = 0; i < v->elements.size(); ++i) if (auto e = v->elements[i]) {
            if (e->id.empty()) continue;
            auto path = "elements[" + e->id + "].";
            scalar(path + "visible", e->visible); scalar(path + "width", e->width, 0, 100000);
            scalar(path + "height", e->height, 0, 100000); scalar(path + "scale", e->scale, .001, 100);
            vector(path + "position", e->position); vector(path + "color", e->color, 4, 0, 1);
        }
    } else if (auto* v = dynamic_cast<code*>(&c)) {
        auto properties = [&](code& script, const std::string& prefix) {
            for (auto& property : script.properties)
                std::visit([&](auto& value) {
                    using type = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<type, double> || std::is_same_v<type, bool>) {
                        const auto path = prefix + property.name;
                        visit(animation_property{path, std::is_same_v<type, bool> ? 0. : -1.e30,
                            std::is_same_v<type, bool> ? 1. : 1.e30, std::is_same_v<type, bool>}, double(value),
                            [&](double number) { script.set_property(property.name, type(number));
                                sol::object object = script.estado_lua["properties"];
                                if (object.is<sol::table>()) object.as<sol::table>()[property.name] = type(number); });
                    } else if constexpr (std::is_same_v<type, std::array<float, 4>>) {
                        for (int axis = 0; axis < 4; ++axis)
                            visit(animation_property{prefix + property.name + "." + std::to_string(axis), 0, 1, false}, double(value[axis]),
                                [&](double n) { auto next = value; next[axis] = float(n); script.set_property(property.name, next);
                                    sol::object object = script.estado_lua["properties"];
                                    if (object.is<sol::table>()) object.as<sol::table>()[property.name] = color{next[0], next[1], next[2], next[3]}; });
                    }
                }, property.value);
        };
        properties(*v, "script[" + v->file + "].");
        for (std::size_t i = 0; i < v->scripts.size(); ++i) if (v->scripts[i])
            properties(*v->scripts[i], "script[" + v->scripts[i]->file + "].");
    } else if (auto* v = dynamic_cast<properties*>(&c)) scalar("active", v->ativo);
    else if (auto* v = dynamic_cast<text*>(&c)) { scalar("screen_position", v->pf_ativa); vector("padding", v->padding, 2, 0, 10000); }
}
}
std::vector<animation_property> animation_properties(component& c) {
    auto result = c.animation_properties();
    channels(c, [&](const auto& property, double, auto) { result.push_back(property); });
    return result;
}
std::optional<double> read_animation_property(component& c, const std::string& path) {
    auto result = c.read_animation_property(path);
    channels(c, [&](const auto& property, double value, auto) { if (property.path == path) result = value; });
    return result;
}
bool write_animation_property(component& c, const std::string& path, double number) {
    if (!std::isfinite(number)) return false;
    bool written = false;
    channels(c, [&](const auto& property, double, auto set) {
        if (property.path == path && number >= property.minimum && number <= property.maximum) {
            set(property.stepped ? std::round(number) : number); written = true;
        }
    });
    return written || c.write_animation_property(path, number);
}
std::shared_ptr<component> animation_target(ecs& registry, uint32_t entity, component::mask mask) {
    if (!registry.is_alive(entity)) return {};
    auto it = registry.entities.find(entity);
    if (it == registry.entities.end()) return {};
    auto target = it->second.find(mask);
    return target == it->second.end() ? nullptr : target->second;
}
void sample_animation(ecs& registry, uint32_t entity, const animation_clip& clip, double seconds) {
    for (const auto& track : clip.tracks) {
        auto value = track.sample(seconds);
        auto target = animation_target(registry, entity, track.target);
        if (value && target) write_animation_property(*target, track.property, *value);
    }
    const auto bone = registry.get<transform>(entity);
    const auto rig = registry.get<renderer>(entity);
    if ((bone && !bone->rig_joint_id.empty()) || (rig && rig->m_modelo && rig->m_modelo->has_skeleton()))
        synchronize_rig_entities(registry);
    if (const auto target = registry.get<renderer>(entity); target && target->m_modelo)
        target->m_modelo->apply_pose();
}
void animation_system::update(const std::shared_ptr<ecs>& registry) {
    const auto dt = m_delta();
    if (!registry || !std::isfinite(dt) || dt < 0) return;
    std::erase_if(m_playback, [&](const auto& entry) {
        const auto clip = entry.second.clip.lock();
        return !clip || registry->get<animation>(entry.first) != clip;
    });
    registry->cada<animation>([&](uint32_t id) {
        auto player = registry->get<animation>(id);
        if (!player) return;
        auto& state = m_playback[id];
        if (state.clip.lock() != player || state.asset.lock() != player->data) {
            state = {};
            state.clip = player; state.asset = player->data; state.current = player->data;
            state.playing = player->autoplay;
        }
        const auto valid = [](const std::shared_ptr<animation_clip>& asset) {
            return asset && std::isfinite(asset->duration) && asset->duration > 0;
        };
        for (const auto& command : player->m_commands) {
            state.manual = true;
            switch (command.op) {
            case animation::operation::pause: state.playing = false; break;
            case animation::operation::stop:
                state.playing = false; state.time = 0; state.channels.clear(); state.source.reset();
                state.sample_requested = true; break;
            case animation::operation::seek:
                if (valid(state.current)) {
                    state.time = std::clamp(command.seconds, 0., state.current->duration);
                    state.sample_requested = true; state.channels.clear(); state.source.reset();
                }
                break;
            case animation::operation::play:
                if (command.asset) {
                    state.current = command.asset; state.time = 0;
                    state.channels.clear(); state.source.reset();
                } else if (valid(state.current) && state.time >= state.current->duration && state.channels.empty()) state.time = 0;
                state.playing = true;
                state.sample_requested = true; break;
            case animation::operation::fade: {
                if (!valid(command.asset)) break;
                // Interrupted transitions start at the currently visible pose.
                auto source = state.channels.empty() ? state.current : nullptr;
                const auto source_time = state.time;
                state.channels.clear();
                auto add_channels = [&](const std::shared_ptr<animation_clip>& asset, bool from) {
                    if (!asset) return;
                    for (std::size_t i = 0; i < asset->tracks.size(); ++i) {
                        const auto& track = asset->tracks[i];
                        auto found = std::find_if(state.channels.begin(), state.channels.end(), [&](const auto& channel) {
                            return channel.mask == track.target && channel.path == track.property;
                        });
                        if (found == state.channels.end()) {
                            auto target = animation_target(*registry, id, track.target);
                            auto value = target ? read_animation_property(*target, track.property) : std::nullopt;
                            if (!value) continue;
                            bool stepped = track.stepped;
                            for (const auto& property : animation_properties(*target))
                                if (property.path == track.property) stepped = stepped || property.stepped;
                            state.channels.push_back({track.target, track.property, target, {}, {}, *value, stepped});
                            found = std::prev(state.channels.end());
                        }
                        found->stepped = found->stepped || track.stepped;
                        (from ? found->source : found->destination) = i;
                    }
                };
                add_channels(source, true); add_channels(command.asset, false);
                state.source = source; state.source_time = source_time;
                state.current = command.asset; state.time = 0;
                state.blend_time = 0; state.blend_duration = command.seconds;
                if (command.seconds == 0) { state.channels.clear(); state.source.reset(); }
                state.playing = true; state.sample_requested = true;
                break;
            }
            }
        }
        player->m_commands.clear();
        if (!state.manual) state.playing = player->autoplay;
        if (!valid(state.current) || !std::isfinite(player->speed) || player->speed < 0 || player->speed > 100) {
            player->m_playing = player->m_blending = false; return;
        }
        if (state.playing || state.sample_requested) {
            if (state.channels.empty()) sample_animation(*registry, id, *state.current, state.time);
            else {
                const double weight = std::clamp(state.blend_time / state.blend_duration, 0., 1.);
                for (const auto& channel : state.channels) {
                    auto target = animation_target(*registry, id, channel.mask);
                    if (!target || target != channel.target.lock()) continue;
                    const auto from = channel.source && state.source && *channel.source < state.source->tracks.size() ? state.source->tracks[*channel.source].sample(state.source_time) : std::nullopt;
                    const auto to = channel.destination && *channel.destination < state.current->tracks.size() ? state.current->tracks[*channel.destination].sample(state.time) : std::nullopt;
                    const double a = from.value_or(channel.fallback), b = to.value_or(channel.fallback);
                    write_animation_property(*target, channel.path, channel.stepped ? (weight < .5 ? a : b) : std::lerp(a, b, weight));
                }
                if (weight >= 1) { state.channels.clear(); state.source.reset(); }
                if (const auto target = registry->get<renderer>(id); target && target->m_modelo)
                    target->m_modelo->apply_pose();
            }
            state.sample_requested = false;
            player->m_time = state.time;
            if (state.playing) {
                const auto advance = [](const animation_clip& asset, double time, double elapsed) {
                    if (asset.loop) return std::fmod(time + std::fmod(elapsed, asset.duration), asset.duration);
                    return std::min(time + elapsed, asset.duration);
                };
                const double elapsed = dt * player->speed;
                if (std::isfinite(elapsed)) {
                    if (!state.current->loop && state.time >= state.current->duration && state.channels.empty()) state.playing = false;
                    else state.time = advance(*state.current, state.time, elapsed);
                    if (valid(state.source)) state.source_time = advance(*state.source, state.source_time, elapsed);
                    if (!state.channels.empty()) state.blend_time = std::min(state.blend_time + elapsed, state.blend_duration);
                }
            }
        }
        player->m_playing = state.playing;
        player->m_blending = !state.channels.empty();
    });
    synchronize_rig_entities(*registry);
}
}
