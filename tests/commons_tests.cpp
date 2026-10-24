#include <gtest/gtest.h>
#include <filesystem>
#include <vector>

#include <glad.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <bgui.hpp>
#include <elem/traffic_light.hpp>
#include <elem/window.hpp>
#include <lay/dock.hpp>
#include <os/style_manager.hpp>
#include <utils/theme.hpp>

#include "components/component.hpp"
#include "components/renderer.hpp"
#include "components/transform.hpp"
#include "core/ecs.hpp"
#include "core/scene.hpp"
#include "entities/entity.hpp"
#include "util/color.hpp"

using namespace commons;

TEST(ColorTest, AddsColorsAndClampsAlpha)
{
    const color result = color(0.2f, 0.3f, 0.4f, 0.7f) + color(0.1f, 0.2f, 0.3f, 0.6f);

    EXPECT_FLOAT_EQ(result.r, 0.3f);
    EXPECT_FLOAT_EQ(result.g, 0.5f);
    EXPECT_FLOAT_EQ(result.b, 0.7f);
    EXPECT_FLOAT_EQ(result.a, 1.0f);
}

TEST(ColorTest, ScalarConstructorSetsOpaqueAlpha)
{
    const color result(0.25f);

    EXPECT_FLOAT_EQ(result.r, 0.25f);
    EXPECT_FLOAT_EQ(result.g, 0.25f);
    EXPECT_FLOAT_EQ(result.b, 0.25f);
    EXPECT_FLOAT_EQ(result.a, 1.0f);
}

TEST(ComponentMaskTest, CombinesAndRemovesComponentFlags)
{
    component::mask mask = component::COMPONENTE_CAM | component::COMPONENTE_RENDER;

    EXPECT_NE(mask & component::COMPONENTE_CAM, component::COMPONENTE_NONE);
    EXPECT_NE(mask & component::COMPONENTE_RENDER, component::COMPONENTE_NONE);

    mask &= ~component::COMPONENTE_CAM;

    EXPECT_EQ(mask & component::COMPONENTE_CAM, component::COMPONENTE_NONE);
    EXPECT_NE(mask & component::COMPONENTE_RENDER, component::COMPONENTE_NONE);
}

TEST(EntityTest, EqualityDependsOnEntityId)
{
    const entity first{7};
    const entity same_id{7};
    const entity other_id{8};

    EXPECT_EQ(first, same_id);
    EXPECT_FALSE(first == other_id);
}

TEST(SceneTest, OwnsAnIndependentEcs)
{
    scene first("First");
    scene second("Second");

    const entity first_entity = first.get_ecs()->create(7);
    const entity second_entity = second.get_ecs()->create(7);

    EXPECT_EQ(first.get_name(), "First");
    EXPECT_EQ(second.get_name(), "Second");
    EXPECT_EQ(first_entity.id, second_entity.id);
    EXPECT_TRUE(first.get_ecs()->has<transform>(first_entity.id));
    EXPECT_TRUE(second.get_ecs()->has<transform>(second_entity.id));
    EXPECT_NE(first.get_ecs(), second.get_ecs());
}

TEST(EditorThemeTest, VerticallyCentersWindowTrafficLights)
{
    const auto theme_path = std::filesystem::path(BENGINE_SOURCE_DIR) /
        "commons/assets/themes/editor-dark.theme";
    const auto editor_overrides = bgui::load_theme(theme_path.string());
    const auto controls = editor_overrides.classes.find("window-controls");

    ASSERT_NE(controls, editor_overrides.classes.end());
    ASSERT_TRUE(controls->second.layout.align.has_value());

    auto& style_manager = bgui::style_manager::get_instance();
    struct theme_restore {
        bgui::style_manager& manager;
        bgui::theme previous;
        ~theme_restore() {
            manager.apply_theme(previous);
            manager.clear_theme_changed_flag();
        }
    } restore{style_manager, style_manager.get_theme()};

    auto theme = bgui::dark_theme();
    theme.classes["window-controls"] = controls->second;
    style_manager.apply_theme(theme);

    {
        bgui::scoped_interface interface;
        auto& dock = bgui::get_layout().add<bgui::dock>();
        auto& panel = dock.add<bgui::window>("Panel", true);
        panel.style.layout.require_size(280.f, 190.f);
        panel.set_position(30, 40);
        panel.mark_style_dirty();
        dock.compute_style();
        dock.process_required_size({900, 600});
        dock.set_final_rect(0, 0, 900, 600);
        dock.cascade_style();
        dock.on_update();
        bgui::element::begin_arranging();
        dock.arrange();
        bgui::element::end_arranging();

        bgui::linear* header = nullptr;
        for (auto& [layer, elements] : panel.get_elements()) {
            (void)layer;
            for (auto& element : elements) {
                if (element->has_class("window-header"))
                    header = dynamic_cast<bgui::linear*>(element.get());
            }
        }
        ASSERT_NE(header, nullptr);

        bgui::linear* controls_layout = nullptr;
        for (auto& [layer, elements] : header->get_elements()) {
            (void)layer;
            for (auto& element : elements) {
                if (element->has_class("window-controls"))
                    controls_layout =
                        dynamic_cast<bgui::linear*>(element.get());
            }
        }
        ASSERT_NE(controls_layout, nullptr);

        std::size_t traffic_light_count = 0;
        for (auto& [layer, elements] : controls_layout->get_elements()) {
            (void)layer;
            for (auto& element : elements) {
                if (!dynamic_cast<bgui::traffic_light*>(element.get()))
                    continue;

                ++traffic_light_count;
                EXPECT_NEAR(
                    element->processed_y() +
                        element->processed_height() / 2.f,
                    header->processed_y() + header->processed_height() / 2.f,
                    1.f
                );
            }
        }
        EXPECT_EQ(traffic_light_count, 3u);
    }
}

TEST(EcsIterationTest, FiltersEntitiesByComponentMask)
{
    auto registry = std::make_shared<ecs>();
    const entity first = registry->create(1);
    entity second = registry->create(2);
    registry->add<renderer>(second);

    std::vector<uint32_t> rendered_entities;
    registry->cada<renderer, transform>([&](const uint32_t id) {
        rendered_entities.push_back(id);
    });

    EXPECT_EQ(rendered_entities, (std::vector<uint32_t>{second.id}));
    EXPECT_TRUE(registry->has<transform>(first.id));
}

namespace {
    struct test_component : commons::component {
        static constexpr commons::component::mask mask = commons::component::COMPONENTE_PROPRIEDADES;
        int value = 0;
        explicit test_component(int v = 0) : value(v) {}
    };
}

TEST(EcsInvariantTest, KeepsLiveEntitySetAndMasksConsistent)
{
    auto registry = std::make_shared<ecs>();
    auto entity = registry->create(7);
    EXPECT_TRUE(registry->is_alive(entity.id));
    EXPECT_TRUE(registry->validate_invariants());

    registry->add<test_component>(entity, 42);
    EXPECT_TRUE(registry->has<test_component>(entity.id));
    EXPECT_EQ(registry->get<test_component>(entity.id)->value, 42);
    EXPECT_TRUE(registry->validate_invariants());

    registry->remove<test_component>(entity.id);
    EXPECT_FALSE(registry->has<test_component>(entity.id));
    EXPECT_TRUE(registry->validate_invariants());

    registry->remove(entity.id);
    EXPECT_FALSE(registry->is_alive(entity.id));
    EXPECT_TRUE(registry->validate_invariants());

    const auto reused = registry->create(entity.id);
    EXPECT_EQ(reused.id, entity.id);
    EXPECT_TRUE(registry->has<transform>(reused.id));
    EXPECT_TRUE(registry->validate_invariants());

    EXPECT_FALSE(registry->has<test_component>(999));
    EXPECT_EQ(registry->get<test_component>(999), nullptr);
    EXPECT_EQ(registry->get<test_component>(entity.id), nullptr);
    EXPECT_FALSE(registry->is_alive(999));
}
