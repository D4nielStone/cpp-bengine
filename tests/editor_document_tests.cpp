#include <gtest/gtest.h>
#include "files_view.hpp"

namespace {
template<class T, class Predicate>
T* find(bgui::element& root, Predicate predicate) {
    if (auto* value = dynamic_cast<T*>(&root); value && predicate(*value)) return value;
    if (auto* layout = root.as_layout())
        for (auto& [layer, children] : layout->get_elements())
            for (auto& child : children)
                if (auto* value = find<T>(*child, predicate)) return value;
    return nullptr;
}
}

TEST(EditorDocuments, DockTabsPreserveBuffersAndSaveWithShortcut) {
    bgui::scoped_interface interface;
    auto& io = bgui::get_context();
    io.m_input_map.clear();
    io.m_char_buffer.clear();
    io.m_size = {900, 600};
    const auto directory = std::filesystem::temp_directory_path() /
        ("bengine-documents-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct cleanup {
        std::filesystem::path directory;
        ~cleanup() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    } cleanup{directory};
    { std::ofstream(directory / "first.lua") << "Input."; }
    { std::ofstream(directory / "second.txt") << "second"; }
    auto& dock = bgui::get_layout().add<bgui::dock>();
    editor::ui::files_view files(directory);
    files.build(dock);
    auto open = [&](const std::string& name) {
        auto* button = find<bgui::button>(dock, [&](auto& value) {
            return find<bgui::text>(value, [&](auto& label) { return label.get_buffer() == name; }) != nullptr;
        });
        ASSERT_NE(button, nullptr);
        button->on_released();
        bgui::on_update();
        files.update();
    };
    open("first.lua");
    auto* first = find<bgui::window>(dock, [](auto& value) { return value.get_title().get_buffer() == "first.lua"; });
    ASSERT_NE(first, nullptr);
    auto* editor = find<editor::ui::lua_inputbox>(*first, [](auto&) { return true; });
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(find<bgui::button>(first->get_context(), [](auto&) { return true; }), nullptr);

    editor->set_buffer("changed");
    editor->set_focused(true);
    files.update();
    EXPECT_EQ(first->get_title().get_buffer(), "first.lua*");
    open("second.txt");
    EXPECT_EQ(editor->get_buffer(), "changed");
    open("first.lua");
    dock.on_update();
    EXPECT_TRUE(first->is_enabled());
    editor->set_focused(true);
    io.m_input_map[bgui::input_key::left_control] = bgui::input_action::press;
    io.m_input_map[bgui::input_key::s] = bgui::input_action::press;
    files.update();
    io.m_input_map.clear();
    EXPECT_EQ(first->get_title().get_buffer(), "first.lua");
    std::ifstream saved(directory / "first.lua");
    std::string content{std::istreambuf_iterator<char>(saved), {}};
    EXPECT_EQ(content, "changed");
    auto config = dock.get_configuration();
    EXPECT_TRUE(std::any_of(config.tab_groups.begin(), config.tab_groups.end(), [](const auto& group) {
        return group.windows.size() == 2;
    }));
}
