#include <gtest/gtest.h>
#include "localization.hpp"
#include "api_reference.hpp"
#include "localized_layout.hpp"
#include <fstream>

using namespace editor::ui;
namespace {
class LocalizationTest : public testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() / "bengine-localization-test";
    void SetUp() override { std::filesystem::remove_all(root); std::filesystem::create_directories(root); }
    void TearDown() override { std::filesystem::remove_all(root); }
};
}
TEST_F(LocalizationTest, FallbackAndMalformedCatalogAreSafe) {
    std::ofstream(root / "en-US.json") << R"({"strings":{"title":"Title","button":"Apply"}})";
    std::ofstream(root / "pt-BR.json") << R"({"strings":{"title":"Título"}})";
    localization catalog(root, "pt-BR");
    EXPECT_EQ(catalog.text("title"), "Título");
    EXPECT_EQ(catalog.text("button"), "Apply");
    EXPECT_EQ(catalog.text("missing"), "missing");
    std::ofstream(root / "pt-BR.json") << R"({"strings":{"title":42}})";
    EXPECT_EQ(localization(root, "pt-BR").text("title"), "Title");
}
TEST_F(LocalizationTest, PreferenceRoundTripsAndPreservesOtherFields) {
    const auto path = root / "preferences.json";
    std::ofstream(path) << R"({"other":true})";
    std::string error;
    ASSERT_TRUE(localization::save_preference(path, "pt-BR", error)) << error;
    EXPECT_EQ(localization::read_preference(path), "pt-BR");
    EXPECT_NE(project_files::read(path).find("other"), std::string::npos);
    EXPECT_FALSE(localization::save_preference(path, "../../bad", error));
    std::ofstream(path) << "broken";
    EXPECT_FALSE(localization::save_preference(path, "en-US", error));
    EXPECT_EQ(project_files::read(path), "broken");
    EXPECT_EQ(localization::read_preference(path), "en-US");
}
TEST(Localization, ShippedCatalogsCoverSameKeysAndMarkdownCatalogIsSearchable) {
    const auto root = std::filesystem::path(COMMONS_ASSET_DIR) / "locales";
    rapidjson::Document en, pt;
    en.Parse(project_files::read(root / "en-US.json").c_str());
    pt.Parse(project_files::read(root / "pt-BR.json").c_str());
    ASSERT_FALSE(en.HasParseError()); ASSERT_FALSE(pt.HasParseError());
    for (const auto& entry : en["strings"].GetObject())
        EXPECT_TRUE(pt["strings"].HasMember(entry.name));
    const auto documents = markdown_catalog(api_reference_documentation_root());
    ASSERT_FALSE(documents.empty());
    const auto found = std::find_if(documents.begin(), documents.end(), [](const auto& document) {
        return markdown_catalog_matches(document, "GetComponent");
    });
    EXPECT_NE(found, documents.end());
}
TEST(Localization, LayoutTitlesAreCanonicalOnlyDuringPersistence) {
    bgui::scoped_interface scope;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    auto& window = dock.add_window(tr("Arquivos"));
    {
        canonical_layout_titles titles;
        EXPECT_EQ(window.get_title().get_buffer(), "Arquivos");
    }
    EXPECT_EQ(window.get_title().get_buffer(), tr("Arquivos"));
}

TEST(Localization, ApiButtonOpensSearchableReference) {
    bgui::scoped_interface scope;
    open_api_reference();
    bgui::inputbox* search = nullptr;
    std::function<void(bgui::layout&)> visit = [&](bgui::layout& layout) {
        for (auto& [layer, children] : layout.get_elements()) for (auto& child : children) {
            if (auto* input = dynamic_cast<bgui::inputbox*>(child.get())) search = input;
            if (auto* nested = child->as_layout()) visit(*nested);
        }
    };
    visit(bgui::get_layout());
    ASSERT_NE(search, nullptr);
    ASSERT_TRUE(search->m_enter_func);
    search->m_enter_func("GetComponent");
    bool found_api_text = false;
    std::function<void(bgui::layout&)> find_api_text = [&](bgui::layout& layout) {
        for (auto& [layer, children] : layout.get_elements()) for (auto& child : children) {
            if (auto* text = dynamic_cast<bgui::text*>(child.get()))
                found_api_text |= text->get_buffer().find("GetComponent") != std::string::npos;
            if (auto* nested = child->as_layout()) find_api_text(*nested);
        }
    };
    find_api_text(bgui::get_layout());
    EXPECT_TRUE(found_api_text);

    search->m_enter_func("query-with-no-markdown-match");
    bool found_empty_state = false;
    find_api_text = [&](bgui::layout& layout) {
        for (auto& [layer, children] : layout.get_elements()) for (auto& child : children) {
            if (auto* text = dynamic_cast<bgui::text*>(child.get()))
                found_empty_state |= text->get_buffer() == tr("No matching Markdown files.");
            if (auto* nested = child->as_layout()) find_api_text(*nested);
        }
    };
    find_api_text(bgui::get_layout());
    EXPECT_TRUE(found_empty_state);
}

TEST_F(LocalizationTest, SavedLayoutsKeepLegacyIdentities) {
    bgui::scoped_interface scope;
    auto& dock = bgui::get_layout().add<bgui::dock>();
    dock.add_window(tr("Arquivos"), bgui::dock_area::bottom);
    dock.add_window(tr("Debug"), bgui::dock_area::bottom);
    const auto path = root / "layout.cfg";
    {
        canonical_layout_titles titles;
        ASSERT_TRUE(bgui::save_configuration(path.string()));
    }
    EXPECT_NE(project_files::read(path).find("\"Arquivos\""), std::string::npos);
    {
        canonical_layout_titles titles;
        ASSERT_TRUE(bgui::load_configuration(path.string()));
    }
    const auto configuration = dock.get_configuration();
    ASSERT_EQ(configuration.windows.size(), 2u);
    EXPECT_EQ(configuration.windows[0].title, tr("Arquivos"));
    EXPECT_EQ(configuration.windows[1].title, tr("Debug"));
}

TEST_F(LocalizationTest, PreferenceAcceptsRelativeDevelopmentPaths) {
    const auto absolute = root / "relative.json";
    const auto relative = std::filesystem::relative(absolute, std::filesystem::current_path());
    std::string error;
    ASSERT_TRUE(localization::save_preference(relative, "pt-BR", error)) << error;
    EXPECT_EQ(localization::read_preference(absolute), "pt-BR");
}

TEST_F(LocalizationTest, MarkdownCatalogRecursivelyOrganizesAndSearchesFiles) {
    std::filesystem::create_directories(root / "guides" / "input");
    std::ofstream(root / "overview.MD") << "# Overview\r\nWelcome to the editor.";
    std::ofstream(root / "guides" / "input" / "keys.markdown")
        << "# Keyboard input\nUse Input.GetKey(\"Space\").";
    std::ofstream(root / "ignored.txt") << "# Not Markdown";

    const auto documents = markdown_catalog(root);
    ASSERT_EQ(documents.size(), 2u);
    EXPECT_EQ(documents[0].category, tr("Documentation"));
    EXPECT_EQ(documents[0].title, "Overview");
    EXPECT_EQ(documents[1].category, "guides/input");
    EXPECT_EQ(documents[1].title, "Keyboard input");
    EXPECT_TRUE(markdown_catalog_matches(documents[1], "INPUT.GETKEY"));
    EXPECT_FALSE(markdown_catalog_matches(documents[1], "no-such-symbol"));
    EXPECT_EQ(markdown_catalog_categories(documents),
        (std::vector<std::string>{tr("Documentation"), "guides/input"}));
}

TEST_F(LocalizationTest, MarkdownRendererDisplaysDiscoveredDocument) {
    const auto document_path = root / "guide.md";
    std::ofstream(document_path) << "# Guide\n\n```lua\nDebug.Log(\"ready\")\n```";
    const auto documents = markdown_catalog(root);
    ASSERT_EQ(documents.size(), 1u);
    bgui::scoped_interface interface;
    bgui::linear body(bgui::orientation::vertical);
    const auto paragraphs = editor::ui::render_markdown(body, documents.front().contents);
    ASSERT_FALSE(paragraphs.empty());
    const auto* heading = dynamic_cast<bgui::text*>(paragraphs.front());
    ASSERT_NE(heading, nullptr);
    EXPECT_EQ(heading->get_buffer(), "Guide");
    bool found_code = false;
    for (const auto* paragraph : paragraphs) {
        const auto* text = dynamic_cast<const bgui::text*>(paragraph);
        ASSERT_NE(text, nullptr);
        EXPECT_EQ(text->get_buffer().find("```"), std::string::npos);
        found_code |= text->get_buffer().find("    Debug.Log") == 0;
    }
    EXPECT_TRUE(found_code);
}

TEST(Localization, ApiReferenceClosesAfterUsingBothFilters) {
    bgui::scoped_interface interface;
    auto& root = bgui::get_layout();
    editor::ui::open_api_reference();
    auto* modal = dynamic_cast<bgui::modal*>(root.get_elements().at(bgui::layer::overlay).front().get());
    ASSERT_NE(modal, nullptr);
    auto& content = modal->content().get_elements().at(bgui::layer::base);
    auto* filters = content.at(2)->as_layout();
    ASSERT_NE(filters, nullptr);
    for (const auto& element : filters->get_elements().at(bgui::layer::base)) {
        auto* filter = dynamic_cast<bgui::combo_box*>(element.get());
        if (!filter) continue;
        filter->on_clicked();
        filter->set_selected_index(1);
    }
    auto& sibling = root.add<bgui::linear, bgui::layer::overlay>(bgui::orientation::vertical);
    auto& panel = *modal->get_elements().at(bgui::layer::base).front()->as_layout();
    auto& actions = *panel.get_elements().at(bgui::layer::base).back()->as_layout();
    auto* close = dynamic_cast<bgui::button*>(actions.get_elements().at(bgui::layer::base).front().get());
    ASSERT_NE(close, nullptr);
    close->on_released();
    bgui::on_update();
    ASSERT_EQ(root.get_elements().at(bgui::layer::overlay).size(), 1u);
    EXPECT_EQ(root.get_elements().at(bgui::layer::overlay).front().get(), &sibling);
}

TEST(Localization, ApiReferencePreservesSelectionAndResizesBothPanes) {
    bgui::scoped_interface interface;
    bgui::get_context().m_size = {1366, 702};
    open_api_reference();
    bgui::inputbox* search = nullptr;
    bgui::text* identity = nullptr;
    api_reference_body* body = nullptr;
    api_reference_article* article = nullptr;
    bgui::linear* index = nullptr;
    bgui::button* guide = nullptr;
    const auto visit = [&](auto&& self, bgui::layout& layout) -> void {
        for (auto& [layer, children] : layout.get_elements()) for (auto& child : children) {
            if (auto* input = dynamic_cast<bgui::inputbox*>(child.get())) search = input;
            if (child->id == "api-document-path") identity = dynamic_cast<bgui::text*>(child.get());
            if (auto* viewport = dynamic_cast<api_reference_body*>(child.get())) body = viewport;
            if (auto* pane = dynamic_cast<api_reference_article*>(child.get())) article = pane;
            if (child->has_class("api-index")) index = dynamic_cast<bgui::linear*>(child.get());
            if (auto* button = dynamic_cast<bgui::button*>(child.get()); button && button->has_class("api-document"))
                if (button->get_label().get_buffer() == "Painéis do editor") guide = button;
            if (auto* nested = child->as_layout()) self(self, *nested);
        }
    };
    visit(visit, bgui::get_layout());
    ASSERT_TRUE(search); ASSERT_TRUE(identity); ASSERT_TRUE(body); ASSERT_TRUE(article); ASSERT_TRUE(index); ASSERT_TRUE(guide);
    EXPECT_EQ(identity->get_buffer(), "lua-api/README.md");
    guide->on_released();
    bgui::on_update();
    EXPECT_EQ(identity->get_buffer(), "EDITOR_PANELS.md");
    search->m_enter_func("Modelagem");
    EXPECT_EQ(identity->get_buffer(), "EDITOR_PANELS.md");
    search->m_enter_func("no-matching-document-12345");
    search->m_enter_func("");
    EXPECT_EQ(identity->get_buffer(), "EDITOR_PANELS.md");
    for (const auto size : {bgui::vec2i{1366,702}, bgui::vec2i{800,600}}) {
        bgui::get_context().m_size = size;
        for (int frame=0; frame<3; ++frame) bgui::on_update();
        EXPECT_LE(body->processed_width(), size.x);
        EXPECT_GT(article->processed_width(), index->processed_width());
        EXPECT_GE(article->processed_x(), index->processed_x()+index->processed_width());
        EXPECT_GT(article->processed_height(), 100);
    }
}
