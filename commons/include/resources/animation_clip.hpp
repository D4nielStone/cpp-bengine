#pragma once
#include "components/component.hpp"
#include <filesystem>
#include <optional>
#include <vector>

namespace commons {
struct animation_key { double time{}, value{}; bool operator==(const animation_key&) const = default; };
struct animation_track {
    component::mask target{component::COMPONENTE_NONE};
    std::string property;
    bool stepped{false};
    std::vector<animation_key> keys;
    bool operator==(const animation_track&) const = default;
    std::optional<double> sample(double time) const;
    void insert(animation_key key);
};

struct animation_clip {
    double duration{5.};
    bool loop{true};
    std::vector<animation_track> tracks;
    std::filesystem::path source_path;
    std::string source_contents, saved_definition;

    bool analyze(const rapidjson::Value&, bool legacy = false);
    bool serialize(rapidjson::Value&, rapidjson::Document::AllocatorType&) const;
    void assign_definition(const animation_clip& value) {
        duration = value.duration; loop = value.loop; tracks = value.tracks;
    }
    std::string encode() const;
    bool dirty() const { return encode() != saved_definition; }
};
std::filesystem::path animation_clip_path(const std::filesystem::path& root, const std::string& reference);
std::shared_ptr<animation_clip> load_animation_clip(const std::filesystem::path& path);
}
