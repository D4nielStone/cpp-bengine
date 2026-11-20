#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include "markdown.hpp"

using namespace editor::ui;

TEST(Markdown, CodeFencesPreserveContentsAndMatchClosingMarker) {
    auto doc = markdown::parse("````cpp\r\n# literal *text*\r\n```\r\n~~~\r\n[x]: https://bad.test\r\n````\r\n# Heading\r\n");
    ASSERT_EQ(doc.blocks.size(), 2u);
    EXPECT_EQ(doc.blocks[0].type, markdown::kind::code);
    EXPECT_EQ(doc.blocks[0].text, "# literal *text*\n```\n~~~\n[x]: https://bad.test");
    EXPECT_TRUE(doc.links.empty());
    EXPECT_EQ(doc.blocks[1].type, markdown::kind::heading);
    EXPECT_EQ(doc.blocks[1].level, 1);
}
TEST(Markdown, TablesSupportAlignmentEscapesAndCodePipes) {
    auto doc = markdown::parse("| A | B | C |\n| :--- | :---: | ---: |\n| a\\|b | `x|y` | z |\n| short |\n");
    ASSERT_EQ(doc.blocks.size(), 1u);
    const auto& table = doc.blocks[0];
    EXPECT_EQ(table.type, markdown::kind::table);
    EXPECT_EQ(table.alignment, (std::vector<int>{-1,0,1}));
    ASSERT_EQ(table.rows.size(), 3u);
    EXPECT_EQ(table.rows[1], (std::vector<std::string>{"a\\|b","`x|y`","z"}));
    EXPECT_EQ(table.rows[2], (std::vector<std::string>{"short","",""}));
    EXPECT_EQ(markdown::parse("a | b\n--- | invalid\n").blocks.size(), 2u);
}
TEST(Markdown, InlineCodeLinksImagesAndReferences) {
    const auto doc = markdown::parse("[ Read Me ]: <guide%20one.md> \"Title\"\n[x]: https://example.com\n");
    auto spans = markdown::inline_spans("`[x] *literal*` [ler][read me] ![foto](images/a.png) <https://example.com> [x]", doc.links);
    ASSERT_EQ(spans.size(), 9u);
    EXPECT_EQ(spans[0].type, markdown::span_kind::code);
    EXPECT_EQ(spans[0].text, "[x] *literal*");
    EXPECT_EQ(spans[2].target, "guide%20one.md");
    EXPECT_EQ(spans[4].type, markdown::span_kind::image);
    EXPECT_EQ(spans[6].type, markdown::span_kind::link);
    EXPECT_EQ(spans[8].target, "https://example.com");
    auto nested = markdown::inline_spans("[link](https://example.com/a(b))");
    ASSERT_EQ(nested.size(), 1u);
    EXPECT_EQ(nested[0].target, "https://example.com/a(b)");
}
TEST(Markdown, MalformedSyntaxAndUnderscoresStayReadable) {
    auto spans = markdown::inline_spans("some_value_with_parts [broken](x `unclosed ![image]");
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].text, "some_value_with_parts [broken](x `unclosed ![image]");
}
TEST(Markdown, ResolvesRelativePathsAndRejectsUnsupportedSchemes) {
    EXPECT_EQ(markdown_local_path("../guide%20one.md#intro", "/docs/api"), "/docs/guide one.md");
    EXPECT_TRUE(markdown_local_path("javascript:alert(1)", "/docs").empty());
    EXPECT_TRUE(markdown_local_path("a%00b", "/docs").empty());
    EXPECT_TRUE(markdown_local_path("#intro", "/docs").empty());
}
TEST(Markdown, LinksUseUnicodePositionsAndInjectedActions) {
    bgui::scoped_interface scope;
    std::string external;
    std::filesystem::path local;
    markdown_text text(markdown::inline_spans("ação [guia](guide.md) `literal [x](bad)` [site](https://example.com)"), .30f,
        {"/docs", [&](const auto& path) { local = path; }, [&](const auto& url) { external = url; }});
    EXPECT_TRUE(text.link_at(4).empty());
    EXPECT_EQ(text.link_at(5), "guide.md");
    EXPECT_TRUE(text.link_at(9).empty());
    text.activate_link(text.link_at(5));
    EXPECT_EQ(local, "/docs/guide.md");
    text.activate_link("https://example.com");
    EXPECT_EQ(external, "https://example.com");
    text.activate_link("javascript:alert(1)");
    EXPECT_EQ(local, "/docs/guide.md");
}
TEST(Markdown, BuildsNativeWidgetsWithoutGraphicsContext) {
    bgui::scoped_interface scope;
    auto& parent = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
    const auto elements = render_markdown(parent,
        "# Title\n> quote\n```lua\nprint('[x](literal)')\n```\n| A | B |\n| --- | --- |\n| `x` | [link](https://example.com) |\n![missing](missing.png)",
        {std::filesystem::temp_directory_path()});
    ASSERT_EQ(elements.size(), 5u);
    EXPECT_TRUE(elements[1]->has_class("markdown-quote"));
    EXPECT_TRUE(elements[2]->has_class("markdown-code"));
    EXPECT_TRUE(elements[3]->has_class("markdown-table"));
    EXPECT_TRUE(elements[4]->has_class("markdown-image-error"));
    for (auto* element : elements) EXPECT_TRUE(parent.remove(element));
}

TEST(Markdown, ImagesHaveRoundedCornersAndRespectAvailableWidth) {
    bgui::scoped_interface scope;
    bgui::texture texture;
    texture.m_size = {1600.f,800.f};
    markdown_image wrapper(texture);
    wrapper.set_final_rect(0,0,400,200);
    wrapper.on_update();
    const auto& children = wrapper.get_elements()[bgui::layer::base];
    ASSERT_EQ(children.size(), 1u);
    auto* image = dynamic_cast<bgui::image*>(children[0].get());
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(*image->style.visual.border_radius, 8.f);
    ASSERT_TRUE(image->style.layout.size);
    EXPECT_FLOAT_EQ(image->style.layout.size->x, 400.f / bgui::get_global_scale());
    EXPECT_FLOAT_EQ(image->style.layout.size->y, 200.f / bgui::get_global_scale());
}

TEST(Markdown, ExternalLinksValidateSchemesAndControlCharacters) {
    EXPECT_TRUE(markdown_external_link("HTTPS://example.com"));
    EXPECT_TRUE(markdown_external_link("mailto:hello@example.com"));
    EXPECT_FALSE(markdown_external_link("javascript:alert(1)"));
    EXPECT_FALSE(markdown_external_link("https://example.com\ninvalid"));
    EXPECT_FALSE(markdown_external_link(std::string("https://example.com\0invalid", 27)));
}

TEST(Markdown, DeferredNavigationSurvivesRemovalOfClickedWidget) {
    bgui::scoped_interface scope;
    bgui::font font;
    font.ascent = 8.f; font.descent = 2.f;
    for (char32_t ch = 32; ch < 127; ++ch) {
        bgui::character glyph;
        glyph.size = {8,10}; glyph.bearing = {0,8}; glyph.advance = 8;
        font.chs.emplace(ch, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("markdown-test", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("markdown-test", resolution);
    auto& parent = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
    std::string opened;
    auto& text = parent.add<markdown_text>(markdown::inline_spans("[site](https://example.com)"), .30f,
        markdown_options{{}, {}, [&](const auto& target) { opened = target; }});
    bgui::get_context().m_size = {800,600};
    bgui::on_update();
    text.set_final_rect(0,0,300,30);
    bgui::get_context().m_mouse_position = {0,0};
    text.on_released();
    EXPECT_TRUE(opened.empty());
    ASSERT_TRUE(parent.remove(&text));
    bgui::on_update();
    EXPECT_EQ(opened, "https://example.com");
}

TEST(Markdown, TextWrappingFollowsPanelWidth) {
    bgui::scoped_interface scope;
    bgui::font font;
    font.ascent = 8.f; font.descent = 2.f;
    for (char32_t ch = 32; ch < 127; ++ch) {
        bgui::character glyph;
        glyph.size = {8,10}; glyph.bearing = {0,8}; glyph.advance = 8;
        font.chs.emplace(ch, glyph);
    }
    constexpr auto resolution = bgui::font_manager::m_default_resolution;
    bgui::asset_manager::get_instance().store_font("markdown-wrap", resolution, std::move(font));
    bgui::font_manager::get_instance().set_default_font("markdown-wrap", resolution);
    bgui::linear parent(bgui::orientation::vertical);
    parent.set_final_rect(0,0,80,200);
    auto& text = parent.add<markdown_text>(markdown::inline_spans(std::string(80, 'x')), 1.f, markdown_options{});
    text.compute_style(); text.on_update(); text.calc_content_size(bgui::layer::base);
    const auto narrow = text.get_content_size();
    EXPECT_LE(narrow.x, 80);
    parent.set_final_rect(0,0,320,200);
    text.on_update(); text.calc_content_size(bgui::layer::base);
    EXPECT_GT(text.get_content_size().x, narrow.x);
    EXPECT_LT(text.get_content_size().y, narrow.y);
}

TEST(Markdown, LoadsLocalImagesRelativeToDocumentWithSpaces) {
    bgui::scoped_interface scope;
    const auto directory = std::filesystem::temp_directory_path() /
        ("bengine-markdown-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct cleanup {
        std::filesystem::path directory;
        ~cleanup() { std::error_code error; std::filesystem::remove_all(directory, error); }
    } cleanup{directory};
    const unsigned char image_bytes[] = {
        137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82, 0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 31, 21, 196, 137, 0, 0, 0, 11, 73, 68, 65, 84, 120, 156, 99, 248, 15, 4, 0, 9, 251, 3, 253, 251, 94, 107, 43, 0, 0, 0, 0, 73, 69, 78, 68, 174, 66, 96, 130
    };
    {
        std::ofstream output(directory / "my image.png", std::ios::binary);
        output.write(reinterpret_cast<const char*>(image_bytes), sizeof(image_bytes));
        ASSERT_TRUE(output.good());
    }
    auto& parent = bgui::get_layout().add<bgui::linear>(bgui::orientation::vertical);
    const auto elements = render_markdown(parent, "![preview](<my image.png>)", {directory});
    ASSERT_EQ(elements.size(), 1u);
    ASSERT_NE(dynamic_cast<markdown_image*>(elements.front()), nullptr);
    const auto& children = elements.front()->as_layout()->get_elements()[bgui::layer::base];
    ASSERT_EQ(children.size(), 1u);
    auto* image = dynamic_cast<bgui::image*>(children.front().get());
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->get_texture().m_size, (bgui::vec2{1.f,1.f}));
}
