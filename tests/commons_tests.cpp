#include <gtest/gtest.h>
#include <vector>

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
