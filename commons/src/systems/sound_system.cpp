#include "profiling/profiler.hpp"
#include "systems/sound_system.hpp"

#include "components/sound_listener.hpp"
#include "components/sound_player.hpp"
#include "components/transform.hpp"

#include <miniaudio.h>
#include <os/resource_files.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace COMMONS_NS {
namespace {

struct voice {
    std::weak_ptr<sound_player> owner;
    std::string clip;
    bgui::resource_files::bytes encoded;
    ma_decoder decoder{};
    ma_sound sound{};
    std::uint64_t restart_revision{};
    bool decoder_ready{false};
    bool sound_ready{false};

    ~voice() {
        if (sound_ready) ma_sound_uninit(&sound);
        if (decoder_ready) ma_decoder_uninit(&decoder);
    }

    ma_result open(ma_engine& engine, const std::filesystem::path& path) {
        auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
        ma_result result;
        if (bgui::resource_files::embedded()) {
            encoded = bgui::resource_files::read(path);
            result = ma_decoder_init_memory(encoded.data(), encoded.size(), &config, &decoder);
        } else {
#ifdef _WIN32
            result = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
            result = ma_decoder_init_file(path.c_str(), &config, &decoder);
#endif
        }
        if (result != MA_SUCCESS) return result;
        decoder_ready = true;

        result = ma_sound_init_from_data_source(&engine, &decoder, 0, nullptr, &sound);
        if (result != MA_SUCCESS) return result;
        sound_ready = true;
        ma_sound_set_attenuation_model(&sound, ma_attenuation_model_linear);
        ma_sound_set_min_distance(&sound, 0.f);
        return MA_SUCCESS;
    }
};

float bounded(float value, float min, float max, float fallback) {
    return std::isfinite(value) ? std::clamp(value, min, max) : fallback;
}

}

struct sound_system::playback {
    ma_engine engine{};
    bool ready{false};
    std::unordered_map<std::uint32_t, std::unique_ptr<voice>> voices;

    ~playback() {
        voices.clear();
        if (ready) ma_engine_uninit(&engine);
    }
};

sound_system::sound_system(std::filesystem::path asset_root, std::uint32_t camera_entity)
    : m_asset_root(std::move(asset_root)), m_camera_entity(camera_entity) {}

sound_system::~sound_system() = default;

void sound_system::setup(const std::shared_ptr<ecs>&) {}

void sound_system::update(const std::shared_ptr<ecs>& registry) {
    BUBBLE_PROFILE("sound", "Audio");
    if (m_device_attempted && !m_playback) return;

    if (m_playback) {
        std::erase_if(m_playback->voices, [&](const auto& entry) {
            const auto player = registry->get<sound_player>(entry.first);
            return !player || (player->playback_request() == sound_player::transport::stopped ||
                (player->playback_request() == sound_player::transport::automatic && !player->play_on_awake)) || !registry->has<transform>(entry.first) ||
                player != entry.second->owner.lock() || player->clip != entry.second->clip;
        });
    }

    std::shared_ptr<transform> listener;
    bool has_explicit_listener = false;
    std::uint32_t listener_id = 0;
    registry->cada<sound_listener>([&](std::uint32_t id) {
        has_explicit_listener = true;
        if (!registry->get<sound_listener>(id)->enabled || (listener && listener_id <= id))
            return;
        if (auto pose = registry->get<transform>(id)) {
            listener = std::move(pose);
            listener_id = id;
        }
    });
    if (!has_explicit_listener)
        listener = registry->get<transform>(m_camera_entity);

    const auto update_listener = [&] {
        if (!listener) return;
        const auto position = listener->get_world_position();
        const auto rotation = listener->get_world_rotation();
        const auto pitch = glm::radians(rotation.x);
        const auto yaw = glm::radians(rotation.y);
        auto& engine = m_playback->engine;
        ma_engine_listener_set_position(&engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(&engine, 0,
            std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    };
    if (m_playback) update_listener();

    registry->cada<sound_player>([&](std::uint32_t id) {
        const auto player = registry->get<sound_player>(id);
        player->report_playing(false);
        const auto request = player->playback_request();
        if (request == sound_player::transport::stopped ||
            (request == sound_player::transport::automatic && !player->play_on_awake) ||
            player->clip.empty()) return;
        if (request == sound_player::transport::paused &&
            (!m_playback || !m_playback->voices.contains(id))) return;
        const auto pose = registry->get<transform>(id);
        if (!pose) return;

        if (!m_device_attempted) {
            m_device_attempted = true;
            auto backend = std::make_unique<playback>();
            const auto result = ma_engine_init(nullptr, &backend->engine);
            if (result != MA_SUCCESS) {
                std::cerr << "[audio] Could not initialize playback: " << ma_result_description(result) << '\n';
                return;
            }
            backend->ready = true;
            m_playback = std::move(backend);
            update_listener();
        }
        if (!m_playback) return;

        auto& slot = m_playback->voices[id];
        const bool first_update = !slot;
        if (first_update) {
            slot = std::make_unique<voice>();
            slot->owner = player;
            slot->clip = player->clip;
            try {
                const auto path = (m_asset_root / std::filesystem::path(player->clip)).lexically_normal();
                const auto result = slot->open(m_playback->engine, path);
                if (result != MA_SUCCESS)
                    std::cerr << "[audio] " << player->clip << ": " << ma_result_description(result) << '\n';
            } catch (const std::exception& error) {
                std::cerr << "[audio] " << player->clip << ": " << error.what() << '\n';
            }
        }
        if (!slot->sound_ready) return;

        const auto source = pose->get_world_position();
        ma_sound_set_position(&slot->sound, source.x, source.y, source.z);
        ma_sound_set_max_distance(&slot->sound, bounded(player->radius, 0.1f, 10000.f, 10.f));
        ma_sound_set_volume(&slot->sound, listener ? bounded(player->volume, 0.f, 1.f, 1.f) : 0.f);
        ma_sound_set_pitch(&slot->sound, bounded(player->pitch, 0.1f, 3.f, 1.f));
        ma_sound_set_looping(&slot->sound, player->looping);
        if (request == sound_player::transport::paused) {
            ma_sound_stop(&slot->sound);
        } else {
            if (slot->restart_revision != player->restart_revision()) {
                ma_sound_seek_to_pcm_frame(&slot->sound, 0);
                slot->restart_revision = player->restart_revision();
                ma_sound_start(&slot->sound);
            } else if (first_update || !ma_sound_is_playing(&slot->sound)) {
                // A completed one-shot remains completed until Play requests a rewind.
                if (!ma_sound_at_end(&slot->sound)) ma_sound_start(&slot->sound);
            }
            player->report_playing(ma_sound_is_playing(&slot->sound));
        }
    });
}

}
