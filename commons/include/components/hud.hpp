#pragma once
#include "component.hpp"
#include <utils/vec.hpp>
#include <optional>
#include <vector>
#include <utility>

namespace commons {
struct hud_element {
    struct legacy_world_text {
        bgui::vec3 position{0.f}, rotation{0.f}, scale{1.f, 1.f, 1.f};
        float depth{0.f};
    };
    std::string kind, text, id, callback;
    bool absolute{false};
    float padding{0}, margin{0};
    int alignment{0};
    bgui::vec4 color{1.f,1.f,1.f,1.f};
    int parent{-1};
    float width{0}, height{0}, scale{0.35f};
    bool visible{true};
    bgui::vec3 position{0.f};
    std::optional<legacy_world_text> legacy_text;
    bool set_screen_position(const bgui::vec3& pixel);
    bool clicked{false};
    bool consume_click() { return std::exchange(clicked, false); }
};
struct hud : component {
    static constexpr component::mask mask = component::COMPONENTE_HUD;
    bool enabled{true};
    std::vector<std::shared_ptr<hud_element>> elements;
    std::shared_ptr<hud_element> add(const std::string& kind, const std::string& text, int parent = -1);
    bool analyze(const rapidjson::Value&) override;
    bool serialize(rapidjson::Value&, rapidjson::Document::AllocatorType&) const override;
};
}
