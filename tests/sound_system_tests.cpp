#include <gtest/gtest.h>
#include "components/sound_player.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include "systems/sound_system.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

TEST(SoundSystem, TransportControlsVoiceAndRejectsRemovedOwner) {
    const auto root = std::filesystem::temp_directory_path() /
        ("bubble-audio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    struct cleanup {
        std::filesystem::path path;
        ~cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{root};
    // A silent mono PCM clip; looped so the test does not depend on wall time.
    std::ofstream file(root / "silence.wav", std::ios::binary);
    auto integer = [&](unsigned value, unsigned bytes) {
        for (unsigned i=0;i<bytes;++i) file.put(static_cast<char>((value >> (8*i)) & 255));
    };
    file.write("RIFF", 4); integer(36+16000,4); file.write("WAVEfmt ",8);
    integer(16,4); integer(1,2); integer(1,2); integer(8000,4); integer(16000,4);
    integer(2,2); integer(16,2); file.write("data",4); integer(16000,4);
    for (unsigned i=0;i<16000;++i) file.put(0);
    file.close();
    auto registry = std::make_shared<commons::ecs>();
    auto entity = registry->create();
    registry->add<commons::transform>(entity);
    registry->add<commons::sound_player>(entity);
    auto player = registry->get<commons::sound_player>(entity.id);
    player->clip = "silence.wav";
    player->looping = true;
    player->volume = 0;
    commons::sound_system system(root, entity.id);
    system.update(registry);
    EXPECT_FALSE(player->is_playing());
    ASSERT_TRUE(player->play());
    system.update(registry);
    if (!player->is_playing()) GTEST_SKIP() << "Audio backend/device unavailable";
    ASSERT_TRUE(player->pause()); system.update(registry);
    EXPECT_FALSE(player->is_playing());
    ASSERT_TRUE(player->resume()); system.update(registry);
    EXPECT_TRUE(player->is_playing());
    ASSERT_TRUE(player->stop()); system.update(registry);
    EXPECT_FALSE(player->is_playing());
    ASSERT_TRUE(player->play()); system.update(registry);
    EXPECT_TRUE(player->is_playing());
    registry->remove<commons::sound_player>(entity.id);
    registry->add<commons::sound_player>(entity);
    system.update(registry);
    EXPECT_FALSE(player->is_playing());
    EXPECT_FALSE(player->play());
    EXPECT_FALSE(registry->get<commons::sound_player>(entity.id)->is_playing());
}
