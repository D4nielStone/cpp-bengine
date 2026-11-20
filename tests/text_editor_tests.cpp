#include <gtest/gtest.h>
#include <bgui.hpp>
#include <elem/inputbox.hpp>
#include <lay/dock.hpp>
#include <os/asset_manager.hpp>

TEST(TextEditor, MultilineCanScrollWithoutFocus) {
    bgui::scoped_interface ui_scope;
    bgui::font font;
    font.ascent = 8.f;
    font.descent = 2.f;
    for (char32_t c = 32; c < 127; ++c) {
        bgui::character glyph;
        glyph.size = {8, 10};
        glyph.bearing = {0, 8};
        glyph.advance = 8;
        font.chs.emplace(c, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("editor-test", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("editor-test", resolution);
    auto& io = bgui::get_context();
    io.m_size = {800, 600};
    std::string content;
    for (int i = 0; i < 100; ++i) content += "line\n";
    auto& input = bgui::get_layout().add<bgui::inputbox>(
        content, "", 0.28f, nullptr, bgui::input_mode::multiline);
    input.style.layout.require_size(300, 100);
    input.get_label().style.layout.require_mode(bgui::mode::wrap_content, bgui::mode::wrap_content);
    input.set_wrap(false);
    input.set_scrollable(true);
    bgui::on_update();
    const auto before = input.get_label().processed_y();
    io.m_mouse_position = {input.processed_x() + 30, input.processed_y() + 30};
    io.m_scroll_delta_y = -2;
    bgui::on_update();
    bgui::on_update();
    EXPECT_LT(input.get_label().processed_y(), before);
}

TEST(TextEditor, DockTabFollowsWindowTitle) {
    bgui::scoped_interface ui_scope;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    auto& first = dock.add_window("file.lua");
    dock.add_window("Other");
    auto config = dock.get_configuration();
    config.tab_groups.push_back({"file.lua", "file.lua", {"file.lua", "Other"}});
    dock.apply_configuration(config);
    first.set_title("file.lua*");
    dock.on_update();
    bool found = false;
    for (auto& [layer, children] : dock.get_elements())
        for (auto& child : children)
            if (child->has_class("dock-tab"))
                for (auto& [tab_layer, tab_children] : child->as_layout()->get_elements())
                    for (auto& tab_child : tab_children)
                        if (auto* label = dynamic_cast<bgui::text*>(tab_child.get()))
                            found |= label->get_buffer() == "file.lua*";
    EXPECT_TRUE(found);
}

TEST(TextEditor, ButtonLabelUsesEllipsisWithoutChangingItsBuffer) {
    bgui::scoped_interface ui_scope;
    bgui::font font;
    font.ascent = 8.f; font.descent = 2.f;
    for (char32_t c = 32; c < 127; ++c) {
        bgui::character glyph;
        glyph.size = {8, 10}; glyph.bearing = {0, 8}; glyph.advance = 8;
        glyph.uv_min = c == U'.' ? bgui::vec2{.5f, 0.f} : bgui::vec2{0.f, 0.f};
        font.chs.emplace(c, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("ellipsis-test", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("ellipsis-test", resolution);
    auto& button = bgui::get_layout().add<bgui::button>("abcdefgh", 1.f, [] {});
    button.style.layout.padding = bgui::vec4i{0};
    bgui::on_update();
    button.set_final_rect(10, 10, 48, 24);
    auto& label = button.get_label();
    label.set_final_rect(10, 10, 120, 20);
    bgui::draw_data data;
    label.get_requires(&data);
    ASSERT_EQ(data.m_quad_requires.size(), 6u);
    for (int i = 0; i < 3; ++i) data.m_quad_requires.pop();
    EXPECT_FLOAT_EQ(data.m_quad_requires.front().m_uv_min.x, .5f);
    EXPECT_EQ(label.get_buffer(), "abcdefgh");
    button.set_final_rect(10, 10, 120, 24);
    bgui::draw_data expanded;
    label.get_requires(&expanded);
    EXPECT_EQ(expanded.m_quad_requires.size(), 8u);
}

TEST(TextEditor, PageNavigationExtendsSelectionAndRespectsDocumentLimits) {
    bgui::scoped_interface ui_scope;
    bgui::get_context().m_input_map.clear();
    bgui::get_context().m_char_buffer.clear();
    bgui::inputbox input("one\ntwo\nthree\nfour\nfive\n", "", .30f, nullptr, bgui::input_mode::multiline);
    input.set_final_rect(0, 0, 200, 100);
    input.set_focused(true);
    auto& io = bgui::get_context();
    io.m_input_map[bgui::input_key::left_shift] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::page_up] = bgui::input_action::press;
    input.on_update();
    EXPECT_EQ(input.get_cursor_position(), 0u);
    io.m_input_map.clear();
    io.m_input_map[bgui::input_key::backspace] = bgui::input_action::press;
    input.on_update();
    EXPECT_TRUE(input.get_buffer().empty());
}

TEST(TextEditor, MouseDragSelectsTextForReplacement) {
    bgui::scoped_interface ui_scope;
    bgui::get_context().m_input_map.clear();
    bgui::get_context().m_char_buffer.clear();
    bgui::font font;
    font.ascent = 8; font.descent = 2;
    for (char32_t c = 32; c < 127; ++c) {
        bgui::character glyph; glyph.size = {8,10}; glyph.bearing = {0,8}; glyph.advance = 8;
        font.chs.emplace(c, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("drag-test", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("drag-test", resolution);
    bgui::inputbox input("abcdef", "", 1.f);
    input.set_final_rect(0, 0, 200, 40);
    input.get_label().set_final_rect(0, 0, 200, 40);
    auto& io = bgui::get_context();
    io.m_mouse_position = {0,1};
    input.on_clicked();
    io.m_input_map[bgui::input_key::mouse_left] = bgui::input_action::press;
    io.m_mouse_position = {24,1};
    input.on_update();
    EXPECT_EQ(input.get_cursor_position(), 3u);
    input.on_released();
    io.m_input_map.clear(); io.m_char_buffer = "X";
    input.on_update();
    EXPECT_EQ(input.get_buffer(), "Xdef");
}

TEST(TextEditor, LongLineScrollsHorizontallyAndFollowsCaretInBothDirections) {
    bgui::scoped_interface ui_scope;
    bgui::font font;
    font.ascent = 8.f; font.descent = 2.f;
    for (char32_t c = 32; c < 127; ++c) {
        bgui::character glyph;
        glyph.size = {8, 10}; glyph.bearing = {0, 8}; glyph.advance = 8;
        font.chs.emplace(c, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("horizontal-editor", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("horizontal-editor", resolution);
    auto& io = bgui::get_context();
    io.m_size = {800, 600};
    auto& input = bgui::get_layout().add<bgui::inputbox>(
        std::string(200, 'a'), "", 1.f, nullptr, bgui::input_mode::multiline);
    input.style.layout.require_size(200, 100);
    input.set_wrap(false);
    input.set_scrollable(true);
    input.set_focused(true);
    for (int i = 0; i < 4; ++i) bgui::on_update();
    EXPECT_TRUE(input.get_horizontal_scrollbar_element().is_enabled());
    io.m_input_map[bgui::input_key::end] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    EXPECT_GT(input.get_horizontal_scroll_offset(), 0);
    io.m_input_map[bgui::input_key::home] = bgui::input_action::press;
    bgui::on_update();
    io.m_input_map.clear();
    bgui::on_update();
    EXPECT_EQ(input.get_horizontal_scroll_offset(), 0);
    input.set_wrap(true);
    for (int i = 0; i < 4; ++i) bgui::on_update();
    EXPECT_FALSE(input.get_horizontal_scrollbar_element().is_enabled());
    EXPECT_GT(input.get_label().get_content_size().y, 10);
    EXPECT_LT(input.get_label().get_vertical_cursor_position(0, 1), 30u);
}
