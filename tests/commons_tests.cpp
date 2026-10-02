#include <gtest/gtest.h>

#include "components/component.hpp"
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