#pragma once

#include "systems/system.hpp"

#include <cstdint>
#include <filesystem>

namespace COMMONS_NS {

class sound_system final : public system {
public:
    sound_system(std::filesystem::path asset_root, std::uint32_t camera_entity);
    ~sound_system() override;

    void set_asset_root(std::filesystem::path root) { m_asset_root = std::move(root); }
    void set_camera_entity(std::uint32_t id) { m_camera_entity = id; }
    void setup(const std::shared_ptr<ecs>& registry) override;
    void update(const std::shared_ptr<ecs>& registry) override;

private:
    struct playback;
    std::unique_ptr<playback> m_playback;
    std::filesystem::path m_asset_root;
    std::uint32_t m_camera_entity;
    bool m_device_attempted{false};
};

}
