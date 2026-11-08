#pragma once

#include "../core/localization.hpp"
#include "markdown.hpp"

#include <bgui.hpp>
#include <panel_facade.hpp>
#include <elem/button.hpp>
#include <elem/combo_box.hpp>
#include <elem/inputbox.hpp>
#include <elem/modal.hpp>
#include <elem/text.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace editor::ui {
struct markdown_document {
    std::filesystem::path path;
    std::string category;
    std::string title;
    std::string contents;
};

inline std::string markdown_catalog_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

inline std::vector<markdown_document> markdown_catalog(const std::filesystem::path& root) {
    if (!std::filesystem::is_directory(root))
        throw std::runtime_error("Markdown directory not found: " + root.string());

    std::vector<markdown_document> documents;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file())
            continue;

        auto extension = markdown_catalog_lower(entry.path().extension().string());
        if (extension != ".md" && extension != ".markdown")
            continue;

        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
            throw std::runtime_error("Could not read Markdown file: " + entry.path().string());
        std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad())
            throw std::runtime_error("Could not read Markdown file: " + entry.path().string());

        const auto relative = std::filesystem::relative(entry.path(), root);
        const auto parent = relative.parent_path();
        const auto category = parent.empty() || parent == "."
            ? tr("Documentation")
            : parent.generic_string();

        std::string title = relative.stem().string();
        size_t line_start = 0;
        while (line_start < contents.size()) {
            const auto line_end = contents.find('\n', line_start);
            const auto line_size = (line_end == std::string::npos ? contents.size() : line_end) - line_start;
            const auto line = contents.substr(line_start, line_size);
            if (line.rfind("# ", 0) == 0) {
                title = line.substr(2);
                if (!title.empty() && title.back() == '\r')
                    title.pop_back();
                break;
            }
            if (line_end == std::string::npos)
                break;
            line_start = line_end + 1;
        }

        documents.push_back({relative, category, std::move(title), std::move(contents)});
    }

    std::sort(documents.begin(), documents.end(), [](const auto& left, const auto& right) {
        if (left.category != right.category)
            return left.category < right.category;
        if (left.title != right.title)
            return left.title < right.title;
        return left.path.generic_string() < right.path.generic_string();
    });
    return documents;
}

inline std::vector<std::string> markdown_catalog_categories(
    const std::vector<markdown_document>& documents) {
    std::vector<std::string> categories;
    for (const auto& document : documents) {
        if (categories.empty() || categories.back() != document.category)
            categories.push_back(document.category);
    }
    return categories;
}

inline bool markdown_catalog_matches(const markdown_document& document, const std::string& query) {
    const auto needle = markdown_catalog_lower(query);
    if (needle.empty())
        return true;
    const auto searchable = markdown_catalog_lower(
        document.title + " " + document.category + " " +
        document.path.generic_string() + " " + document.contents);
    return searchable.find(needle) != std::string::npos;
}

class api_reference_body final : public bgui::linear {
public:
    api_reference_body() : bgui::linear(bgui::orientation::vertical) {
        set_scrollable(false);
    }

    void on_update() override {
        const auto viewport = bgui::get_context_size();
        const auto scale = bgui::get_global_scale();
        const bgui::vec2 size{std::clamp(viewport.x / scale - 80.f, 120.f, 680.f),
            std::clamp(viewport.y / scale - 180.f, 60.f, 420.f)};
        if (size.x != m_size.x || size.y != m_size.y) {
            m_size = size;
            style.layout.require_size(size.x, size.y);
            mark_layout_dirty();
        }
        bgui::linear::on_update();
    }

private:
    bgui::vec2 m_size{};
};

inline std::filesystem::path api_reference_documentation_root() {
#ifdef BENGINE_REPOSITORY_ROOT
    return editor::distribution::path("docs", BENGINE_REPOSITORY_ROOT "/docs");
#else
    return std::filesystem::current_path() / "docs";
#endif
}

inline void open_api_reference() {
    auto& modal = bgui::get_layout().add<bgui::modal, bgui::layer::overlay>();
    auto content = bgui::panel_content(modal.content());
    content.label(tr("API Reference"), .4f);
    auto& search = content.add<bgui::inputbox>("", tr("Search documentation"), .3f);

    std::vector<markdown_document> documents;
    std::string load_error;
    try {
        documents = markdown_catalog(api_reference_documentation_root());
    } catch (const std::exception& error) {
        load_error = error.what();
    }
    auto categories = markdown_catalog_categories(documents);
    std::vector<std::string> category_options{tr("All categories")};
    category_options.insert(category_options.end(), categories.begin(), categories.end());
    auto& filters = content.row();
    filters.native().set_scrollable(true);
    filters.native().style.layout.require_height(bgui::mode::pixel, 36);
    auto& category = filters.add<bgui::combo_box>(category_options, .28f);
    category.style.layout.require_width(bgui::mode::pixel, 260);

    auto& viewport = content.add<api_reference_body>();
    auto& panes = viewport.add<bgui::linear>(bgui::orientation::horizontal);
    panes.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);

    auto& index = panes.add<bgui::linear>(bgui::orientation::vertical);
    index.set_scrollable(true);
    index.style.layout.require_mode(bgui::mode::pixel, bgui::mode::match_parent);
    index.style.layout.require_width(bgui::mode::pixel, 220.f);

    auto& article = panes.add<bgui::linear>(bgui::orientation::vertical);
    article.set_scrollable(true);
    article.style.layout.require_mode(bgui::mode::stretch, bgui::mode::match_parent);

    auto state = std::make_shared<std::vector<markdown_document>>(std::move(documents));
    auto show_document = [state, &article, load_error](std::size_t selected) {
        while (!article.get_elements()[bgui::layer::base].empty())
            article.remove(article.get_elements()[bgui::layer::base].back().get());

        if (!load_error.empty()) {
            auto& error = article.add<bgui::text>(load_error, .28f);
            error.set_wrap(true);
            return;
        }
        if (selected >= state->size()) {
            auto& message = article.add<bgui::text>(
                state->empty() ? tr("No Markdown files found.") : tr("No matching Markdown files."), .28f);
            message.set_wrap(true);
            return;
        }
        render_markdown(article, (*state)[selected].contents);
    };

    auto refresh = [state, &search, &category, &index, show_document, categories, load_error] {
        while (!index.get_elements()[bgui::layer::base].empty())
            index.remove(index.get_elements()[bgui::layer::base].back().get());

        std::vector<std::size_t> matches;
        const auto selected_category = category.get_selected_index() == 0
            ? std::string{}
            : categories[category.get_selected_index() - 1];
        for (std::size_t i = 0; i < state->size(); ++i) {
            const auto& document = (*state)[i];
            if ((!selected_category.empty() && document.category != selected_category) ||
                !markdown_catalog_matches(document, search.get_buffer()))
                continue;
            matches.push_back(i);
        }

        std::string current_category;
        for (const auto selected : matches) {
            const auto& document = (*state)[selected];
            if (document.category != current_category) {
                current_category = document.category;
                auto& heading = index.add<bgui::text>(current_category, .27f);
                heading.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
            }
            const auto label = document.title + " [" + document.path.filename().string() + "]";
            auto& button = index.add<bgui::button>(label, .25f,
                [show_document, selected] { show_document(selected); });
            button.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        }

        if (!load_error.empty())
            show_document(state->size());
        else if (matches.empty())
            show_document(state->empty() ? state->size() : state->size() + 1);
        else
            show_document(matches.front());
    };

    refresh();
    search.m_enter_func = [&search, refresh](const std::string& query) {
        search.set_buffer(query);
        refresh();
    };
    category.set_on_change([refresh](std::size_t, const std::string&) { refresh(); });
    modal.set_confirmation_text(tr("Close"));
}
}
