#include <gtest/gtest.h>
#include "ui_scale_system.hpp"

namespace {
class UiScaleTest : public testing::Test {
protected:
    bgui::scoped_interface interface;
    editor::ui::ui_scale_system shortcuts;
    void SetUp() override {
        bgui::get_context().m_input_map.clear();
        bgui::set_global_scale(1.f);
    }
    void TearDown() override { bgui::set_global_scale(1.f); }
    void press(bgui::input_key key) {
        bgui::get_context().m_input_map[key] = bgui::input_action::press;
    }
    void release(bgui::input_key key) {
        bgui::get_context().m_input_map[key] = bgui::input_action::release;
        shortcuts.on_update();
    }
};
}

TEST_F(UiScaleTest, ControlPlusAndMinusChangeScaleOncePerPress) {
    press(bgui::input_key::left_control);
    press(bgui::input_key::equal);
    press(bgui::input_key::left_shift);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.1f);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.1f);
    release(bgui::input_key::equal);
    press(bgui::input_key::equal);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.2f);
    release(bgui::input_key::equal);
    press(bgui::input_key::minus);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.1f);
}

TEST_F(UiScaleTest, PlainTypingDoesNotZoom) {
    press(bgui::input_key::equal);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
    release(bgui::input_key::equal);
    press(bgui::input_key::minus);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
}

TEST_F(UiScaleTest, KeypadAndRightControlSupportZoomAndReset) {
    press(bgui::input_key::right_control);
    press(bgui::input_key::keypad_add);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.1f);
    release(bgui::input_key::keypad_add);
    press(bgui::input_key::keypad_subtract);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
    bgui::set_global_scale(1.5f);
    press(bgui::input_key::keypad_zero);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
    release(bgui::input_key::keypad_zero);
    bgui::set_global_scale(.5f);
    press(bgui::input_key::zero);
    shortcuts.on_update();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
}

TEST_F(UiScaleTest, MenuActionsClampScaleAndReturnToOneHundredPercent) {
    for (int i = 0; i < 30; ++i)
        editor::ui::change_ui_scale(1);
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 2.f);
    for (int i = 0; i < 30; ++i)
        editor::ui::change_ui_scale(-1);
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), .5f);
    editor::ui::reset_ui_scale();
    EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.f);
}

TEST_F(UiScaleTest, ZoomReflowsPixelSizedElements) {
    auto& element = bgui::get_layout().add<bgui::element>();
    element.style.layout.require_size(100.f, 40.f);
    bgui::on_update();
    EXPECT_EQ(element.processed_width(), 100);
    EXPECT_EQ(element.processed_height(), 40);
    editor::ui::change_ui_scale(5);
    bgui::on_update();
    EXPECT_EQ(element.processed_width(), 150);
    EXPECT_EQ(element.processed_height(), 60);
}
