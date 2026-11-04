#include "editor_api.hpp"
#include "editor_commands.hpp"
#include "editor_viewport.hpp"
#include "primitive_catalog.hpp"

#if __has_include(<bgui.hpp>)
#error The editor core must compile without BGUI include paths.
#endif

#include <gtest/gtest.h>

TEST(EditorCore, TracksIndependentEventRevisions) {
    editor::ui::editor_events events;
    using editor::ui::editor_event;
    events.publish(editor_event::selection);
    events.publish(editor_event::history);
    events.publish(editor_event::selection);
    EXPECT_EQ(events.revision(editor_event::selection), 2u);
    EXPECT_EQ(events.revision(editor_event::history), 1u);
    EXPECT_EQ(events.revision(editor_event::scene), 0u);
}
