#include <gtest/gtest.h>
#include <bgui.hpp>
#include <elem/inputbox.hpp>
#include <lay/dock.hpp>
#include <os/asset_manager.hpp>

TEST(TextEditor, MultilineCanScrollWithoutFocus) {
    bgui::scoped_interface interface;
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
    bgui::scoped_interface interface;
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
