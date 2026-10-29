#include <gtest/gtest.h>
#include "lua_inputbox.hpp"
using editor::ui::complete_lua;

TEST(LuaCompletion, FiltersMembersAndInfersTransformAliases) {
    auto result = complete_lua("transform:", 10);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->members, (std::vector<std::string>{"Rotate", "Translate"}));
    const std::string source = "local tr = gameObject:GetComponent(\"Transform\")\ntr:Tr";
    result = complete_lua(source, source.size());
    ASSERT_TRUE(result);
    EXPECT_EQ(result->members, (std::vector<std::string>{"Translate"}));
    EXPECT_EQ(source.substr(result->begin, result->end - result->begin), "Tr");
    result = complete_lua("bubble.input.", 13);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->members.size(), 3u);
    result = complete_lua("Input.GetAx", 11);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->members.front(), "GetAxis");
    result = complete_lua("Input.GetKy", 9);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->begin, 6u);
    EXPECT_EQ(result->end, 11u); // Replace the whole token, including the suffix after the caret.
}

TEST(LuaCompletion, IgnoresUnknownObjectsCommentsAndStrings) {
    for (const std::string source : {"unknown.", "-- Input.", "print('Input.", "[[transform:", "--[[\nInput.", "Time:"})
        EXPECT_FALSE(complete_lua(source, source.size())) << source;
    const std::string reassigned = "local tr = transform\ntr = 42\ntr:";
    EXPECT_FALSE(complete_lua(reassigned, reassigned.size()));
}

TEST(LuaCompletion, KeyboardAcceptEscapeAndUndo) {
    bgui::scoped_interface interface;
    auto& io = bgui::get_context();
    io.m_input_map.clear();
    io.m_char_buffer.clear();
    editor::ui::lua_inputbox input("Input.Get");
    input.set_focused(true);
    input.on_update();
    io.m_input_map[bgui::input_key::down] = bgui::input_action::press;
    input.on_update();
    io.m_input_map[bgui::input_key::down] = bgui::input_action::none;
    io.m_input_map[bgui::input_key::tab] = bgui::input_action::press;
    input.on_update();
    EXPECT_EQ(input.get_buffer(), "Input.GetKey");
    input.on_update(); // Holding the accept key must not insert a literal tab.
    EXPECT_EQ(input.get_buffer(), "Input.GetKey");
    io.m_input_map[bgui::input_key::tab] = bgui::input_action::none;
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::z] = bgui::input_action::press;
    input.on_update();
    EXPECT_EQ(input.get_buffer(), "Input.Get");
    io.m_input_map.clear();
    io.m_input_map[bgui::input_key::escape] = bgui::input_action::press;
    input.on_update();
    io.m_input_map[bgui::input_key::escape] = bgui::input_action::none;
    io.m_input_map[bgui::input_key::tab] = bgui::input_action::press;
    input.on_update();
    EXPECT_EQ(input.get_buffer(), "Input.Get\t");
    io.m_input_map.clear();
}

TEST(LuaCompletion, MouseAcceptanceRestoresTypingFocus) {
    bgui::scoped_interface interface;
    auto& io = bgui::get_context();
    io.m_input_map.clear();
    io.m_char_buffer.clear();
    auto& input = bgui::get_layout().add<editor::ui::lua_inputbox>("Debug.");
    bgui::focus_keyboard(input);
    input.on_update();
    auto& overlay = input.get_elements()[bgui::layer::overlay];
    ASSERT_EQ(overlay.size(), 1u);
    auto* popup = overlay.front()->as_layout();
    ASSERT_NE(popup, nullptr);
    auto& options = popup->get_elements()[bgui::layer::base];
    ASSERT_EQ(options.size(), 1u);
    auto* button = dynamic_cast<bgui::button*>(options.front().get());
    ASSERT_NE(button, nullptr);
    bgui::clear_keyboard_focus();
    button->on_released();
    bgui::on_update();
    EXPECT_EQ(input.get_buffer(), "Debug.Log");
    EXPECT_EQ(bgui::get_keyboard_focus(), &input);
    io.m_char_buffer = "(";
    bgui::on_update();
    EXPECT_EQ(input.get_buffer(), "Debug.Log(");
}

TEST(LuaCompletion, InfersWritableComponentProperties) {
    for (const auto& [type, property] : std::vector<std::pair<std::string, std::string>>{
        {"Rigidbody", "mass"}, {"Camera", "fieldOfView"},
        {"DirectionalLight", "direction"}, {"PointLight", "intensity"}}) {
        const auto source = "local component = gameObject:GetComponent(\"" + type + "\")\ncomponent." + property;
        const auto result = complete_lua(source, source.size());
        ASSERT_TRUE(result) << source;
        EXPECT_EQ(result->members, (std::vector<std::string>{property}));
    }
}
