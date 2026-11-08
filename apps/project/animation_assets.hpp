#pragma once
#include "components/animation.hpp"
#include "project_file_transaction.hpp"

namespace editor::ui {
inline bool save_animation_asset(commons::animation_clip& clip, std::string& error) {
    try {
        if (clip.source_path.empty()) throw std::runtime_error("Create an animation clip file first.");
        const auto encoded = clip.encode();
        if (!project_files::recover(clip.source_path, error)) return false;
        const bool exists = std::filesystem::exists(clip.source_path);
        if (exists) {
            const auto disk = project_files::read(clip.source_path);
            if (disk != clip.source_contents)
                throw std::runtime_error("Animation clip changed on disk; reload it before saving: " + clip.source_path.string());
        } else if (!clip.source_contents.empty()) {
            throw std::runtime_error("Animation clip was removed; choose another file instead of overwriting it.");
        }
        if (!exists || clip.dirty()) {
            std::filesystem::create_directories(clip.source_path.parent_path());
            if (!project_files::save(clip.source_path, {{clip.source_path, encoded}}, error)) return false;
            clip.source_contents = encoded;
        }
        clip.saved_definition = encoded;
        return true;
    } catch (const std::exception& failure) { error = failure.what(); return false; }
}

struct animation_asset_save {
    std::shared_ptr<commons::animation> player;
    std::shared_ptr<commons::animation_clip> asset;
    std::string reference;
    bool migration{false};
    void finish() const {
        if (!migration) return;
        player->clip = reference;
        player->data->source_path = asset->source_path;
        player->data->source_contents = asset->source_contents;
        player->data->saved_definition = asset->saved_definition;
    }
};
inline animation_asset_save prepare_animation_asset(
    const std::shared_ptr<commons::animation>& player, commons::ecs& registry,
    const std::filesystem::path& root, uint32_t id) {
    std::string error;
    if (!commons::resolve_animation_clip(registry, *player, root, error)) throw std::runtime_error(error);
    animation_asset_save result{player, player->data, player->clip, false};
    if (!player->clip.empty() || !player->data) return result;
    result.migration = true;
    result.asset = std::make_shared<commons::animation_clip>(*player->data);
    const auto encoded = result.asset->encode();
    for (unsigned suffix = 0; suffix < 10000; ++suffix) {
        result.reference = "assets/animations/legacy-" + std::to_string(id) +
            (suffix ? "-" + std::to_string(suffix) : "") + ".banim";
        result.asset->source_path = commons::animation_clip_path(root, result.reference);
        result.asset->source_contents.clear(); result.asset->saved_definition.clear();
        if (!std::filesystem::exists(result.asset->source_path)) return result;
        if (std::filesystem::is_regular_file(result.asset->source_path) &&
            project_files::read(result.asset->source_path) == encoded) {
            result.asset->source_contents = encoded; result.asset->saved_definition = encoded;
            return result;
        }
    }
    throw std::runtime_error("Could not allocate a file for the legacy animation clip.");
}
}
