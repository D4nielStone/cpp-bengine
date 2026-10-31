#include <gtest/gtest.h>
#include <bgui.hpp>
#include <elem/button.hpp>
#include <elem/inputbox.hpp>
#include <elem/modal.hpp>
#include "asset_drop_overlay.hpp"

namespace {
class EditorOverlayInputTest : public testing::Test {
protected:
    bgui::scoped_interface ui_scope;
    std::shared_ptr<editor::ui::editor_context> context =
        std::make_shared<editor::ui::editor_context>(std::make_shared<commons::ecs>());

    void SetUp() override {
        auto& io = bgui::get_context();
        io.m_size = {900, 600};
        io.m_input_map.clear();
        io.m_last_mouse_left = false;
        io.m_mouse_position = {0, 0};
        editor::ui::asset_drag_active = false;
    }
    void TearDown() override { editor::ui::asset_drag_active = false; }
    void frame() {
        bgui::on_update();
        auto* draw = bgui::get_draw_data();
        while (!draw->m_quad_requires.empty()) draw->m_quad_requires.pop();
        draw->m_draw_list.clear();
    }
    void click(bgui::element& element) {
        auto& io = bgui::get_context();
        io.m_mouse_position = {element.processed_x() + element.processed_width() / 2,
            element.processed_y() + element.processed_height() / 2};
        io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
        frame();
        io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
        frame();
        frame(); // Button callbacks run in the next mutation phase.
    }
};
}

TEST_F(EditorOverlayInputTest, DropHintAllowsClicksWhenHiddenVisibleAndHiddenAgain) {
    int clicks = 0;
    auto& root = bgui::get_layout();
    auto& button = root.add<bgui::button>("Action", .3f, [&] { ++clicks; });
    button.style.layout.require_size(200.f, 100.f);
    bool native_hover = false;
    root.add<editor::ui::asset_drop_overlay, bgui::layer::overlay>(
        [&] { return native_hover; }, context);
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 1);
    native_hover = true;
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 2);
    native_hover = false;
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 3);
    editor::ui::asset_drag_active = true;
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 4);
}

TEST_F(EditorOverlayInputTest, HiddenDropHintAllowsFocusingAndTypingInFields) {
    auto& root = bgui::get_layout();
    auto& input = root.add<bgui::inputbox>("", "Value", .3f);
    input.style.layout.require_size(200.f, 100.f);
    root.add<editor::ui::asset_drop_overlay, bgui::layer::overlay>([] { return false; }, context);
    frame();
    frame();
    click(input);
    ASSERT_EQ(bgui::get_keyboard_focus(), &input);
    bgui::get_context().m_char_buffer = "hello";
    frame();
    EXPECT_EQ(input.get_buffer(), "hello");
}

TEST_F(EditorOverlayInputTest, OrdinaryOverlaysStillBlockControlsBehindThem) {
    int clicks = 0;
    auto& root = bgui::get_layout();
    auto& button = root.add<bgui::button>("Action", .3f, [&] { ++clicks; });
    button.style.layout.require_size(200.f, 100.f);
    auto& blocker = root.add<bgui::linear, bgui::layer::overlay>();
    blocker.receives_input(false);
    blocker.set_flex(false);
    blocker.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 0);
    blocker.set_input_passthrough(true);
    click(button);
    EXPECT_EQ(clicks, 1);
}

TEST_F(EditorOverlayInputTest, DropHintDoesNotBypassModalInputBlocking) {
    int clicks = 0;
    auto& root = bgui::get_layout();
    auto& button = root.add<bgui::button>("Action", .3f, [&] { ++clicks; });
    button.style.layout.require_size(200.f, 100.f);
    auto& modal = root.add<bgui::modal, bgui::layer::overlay>();
    modal.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    root.add<editor::ui::asset_drop_overlay, bgui::layer::overlay>([] { return true; }, context);
    frame();
    frame();
    EXPECT_TRUE(editor::ui::viewport_input_blocked_by_modal());
    click(button);
    EXPECT_EQ(clicks, 0);
    modal.set_enable(false);
    frame();
    frame();
    click(button);
    EXPECT_EQ(clicks, 1);
}

#include "../commons/src/os/file_drag_session.hpp"

TEST(FileDragSessionTest, RetainedSelectionAndOrdinaryClicksAreNotAFileOffer) {
    commons::detail::file_drag_session drag;
    EXPECT_FALSE(drag.hovered(42, true, true));
    drag.enter(42, false); // Plain text / editor widgets do not offer files.
    EXPECT_FALSE(drag.hovered(42, true, true));
    drag.enter(42, true);
    EXPECT_TRUE(drag.hovered(42, true, true));
    EXPECT_FALSE(drag.hovered(42, false, true));
    EXPECT_TRUE(drag.hovered(42, true, true));
    drag.leave(42);
    EXPECT_FALSE(drag.hovered(42, true, true));
}

TEST(FileDragSessionTest, ReleaseOwnerChangeAndLeaveEndTheFileSession) {
    commons::detail::file_drag_session drag;
    drag.enter(42, true);
    EXPECT_FALSE(drag.hovered(42, true, false));
    EXPECT_FALSE(drag.hovered(42, true, true)); // A later click cannot revive it.
    drag.enter(42, true);
    EXPECT_FALSE(drag.hovered(43, true, true));
    EXPECT_FALSE(drag.hovered(42, true, true));
    drag.enter(42, true);
    drag.leave(43); // Unrelated native messages cannot cancel this session.
    EXPECT_TRUE(drag.hovered(42, true, true));
    drag.leave(42); // Also used for drop/cancel.
    EXPECT_FALSE(drag.hovered(42, true, true));
}

TEST_F(EditorOverlayInputTest, InternalHintRequiresHeldButtonAndCursorInsideWindow) {
    auto& overlay = bgui::get_layout().add<editor::ui::asset_drop_overlay, bgui::layer::overlay>(
        [] { return false; }, context);
    auto& io = bgui::get_context();
    io.m_mouse_position = {100, 100};
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    editor::ui::asset_drag_active = true;
    frame();
    EXPECT_TRUE(*overlay.style.visual.visible);
    io.m_mouse_position = {-1, 100};
    frame();
    EXPECT_FALSE(*overlay.style.visual.visible);
    io.m_mouse_position = {100, 100};
    frame();
    EXPECT_TRUE(*overlay.style.visual.visible);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::release;
    frame();
    EXPECT_FALSE(*overlay.style.visual.visible);
    EXPECT_FALSE(editor::ui::asset_drag_active);
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    frame();
    EXPECT_FALSE(*overlay.style.visual.visible);
}
