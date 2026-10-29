#include <gtest/gtest.h>
#include "files_view.hpp"
#include "debug_view.hpp"

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
    EXPECT_EQ(editor->processed_x(), first->get_context().processed_x());
    EXPECT_EQ(editor->processed_y(), first->get_context().processed_y());
    EXPECT_EQ(editor->get_parent()->computed_style.layout.padding, bgui::vec4i{0});
    EXPECT_EQ(editor->computed_style.layout.padding, bgui::vec4i{0});
    EXPECT_EQ(editor->processed_y() + editor->processed_height(),
        first->get_context().processed_y() + first->get_context().processed_height());
    EXPECT_EQ(editor->get_label().computed_style.layout.margin, bgui::vec4i{0});

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

TEST(EditorDocuments, DebugSharesFilesTabAndDisplaysScriptHistory) {
    bgui::scoped_interface interface;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    dock.add_window("Arquivos", bgui::dock_area::bottom, .3f);
    editor::ui::debug_view debug;
    auto& log = commons::script_log::instance();
    log.clear();
    debug.build(dock);
    auto* window = find<bgui::window>(dock, [](auto& w) { return w.get_title().get_buffer() == "Debug"; });
    ASSERT_NE(window, nullptr);
    EXPECT_TRUE(window->has_class("editor-surface"));
    auto config = dock.get_configuration();
    ASSERT_EQ(config.tab_groups.size(), 1u);
    EXPECT_EQ(config.tab_groups.front().windows, (std::vector<std::string>{"Arquivos", "Debug"}));
    log.write(12, "script message");
    debug.update();
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) {
        return text.get_buffer().find("[lua][12] script message") != std::string::npos;
    }), nullptr);
    auto* clear = find<bgui::button>(dock, [](auto& button) {
        return button.get_label().get_buffer() == "Limpar";
    });
    ASSERT_NE(clear, nullptr);
    clear->on_released();
    bgui::on_update();
    debug.update();
    EXPECT_TRUE(log.lines().empty());
    EXPECT_NE(find<bgui::text>(dock, [](auto& text) {
        return text.get_buffer() == "Nenhum log Lua.";
    }), nullptr);
}

TEST(EditorDocuments, SessionRestoresOpenFilesDirtyBuffersAndScale) {
    const auto directory = std::filesystem::temp_directory_path() /
        ("bengine-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct cleanup {
        std::filesystem::path path;
        ~cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
    } cleanup{directory};
    const auto source = directory / "file with spaces.lua";
    const auto session = directory / "session.cfg";
    { std::ofstream(source) << "original"; }
    {
        bgui::scoped_interface interface;
        bgui::get_context().m_input_map.clear();
        bgui::get_context().m_size = {900, 600};
        auto& dock = bgui::get_layout().add<bgui::dock>();
        editor::ui::files_view files(directory);
        files.build(dock);
        auto* button = find<bgui::button>(dock, [&](auto& value) {
            return find<bgui::text>(value, [&](auto& label) { return label.get_buffer() == source.filename().string(); });
        });
        ASSERT_NE(button, nullptr);
        button->on_released();
        bgui::on_update();
        auto* editor = find<editor::ui::lua_inputbox>(dock, [](auto&) { return true; });
        ASSERT_NE(editor, nullptr);
        editor->set_buffer("changed\nprint(\"quoted\")");
        bgui::set_global_scale(1.3f);
        auto configuration = dock.get_configuration();
        configuration.left_ratio = .35f;
        configuration.windows.front().floating = true;
        configuration.windows.front().rect = {40, 50, 420, 280};
        dock.apply_configuration(configuration);
        ASSERT_TRUE(files.save_session(session));
        ASSERT_TRUE(bgui::save_configuration((directory / "layout.cfg").string()));
        ASSERT_TRUE(files.save_session(session)); // Replacing an existing session must work too.
    }
    {
        bgui::scoped_interface interface;
        bgui::get_context().m_input_map.clear();
        bgui::get_context().m_size = {900, 600};
        bgui::set_global_scale(1.f);
        auto& dock = bgui::get_layout().add<bgui::dock>();
        editor::ui::files_view files(directory);
        files.build(dock);
        files.restore_session(session);
        auto* editor = find<editor::ui::lua_inputbox>(dock, [](auto&) { return true; });
        ASSERT_NE(editor, nullptr);
        EXPECT_EQ(editor->get_buffer(), "changed\nprint(\"quoted\")");
        EXPECT_FLOAT_EQ(bgui::get_global_scale(), 1.3f);
        ASSERT_TRUE(bgui::load_configuration((directory / "layout.cfg").string()));
        EXPECT_FLOAT_EQ(dock.get_configuration().left_ratio, .35f);
        const auto restored = dock.get_configuration();
        const auto floating = std::find_if(restored.windows.begin(), restored.windows.end(),
            [](const auto& window) { return window.title == "Arquivos"; });
        ASSERT_NE(floating, restored.windows.end());
        EXPECT_TRUE(floating->floating);
        EXPECT_NE(find<bgui::window>(dock, [](auto& value) {
            return value.get_title().get_buffer() == "file with spaces.lua*";
        }), nullptr);
        files.set_project_file(directory / "other.bengine");
        EXPECT_EQ(find<editor::ui::lua_inputbox>(dock, [](auto&) { return true; }), nullptr);
        std::filesystem::remove(source);
        files.restore_session(session); // Missing files are ignored.
        EXPECT_EQ(find<editor::ui::lua_inputbox>(dock, [](auto&) { return true; }), nullptr);
        bgui::set_global_scale(1.f);
    }
}
