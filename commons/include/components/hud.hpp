#pragma once
#include "component.hpp"
#include <utils/vec.hpp>
#include <vector>
#include <utility>

namespace commons {
struct hud_element {
    std::string kind, text, id, callback;
    bool absolute{false};
    float padding{0}, margin{0};
    int alignment{0};
    bgui::vec4 color{1.f,1.f,1.f,1.f};
    int parent{-1};
    float width{0}, height{0}, scale{0.35f};
    bool visible{true};
    bgui::vec3 position{0.f};
    bgui::vec3 rotation{0.f}, world_scale{1.f};
    float depth{0.f};
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
