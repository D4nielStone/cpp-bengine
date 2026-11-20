#pragma once

#include "markdown_parse.hpp"
#include "../systems/desktop_link.hpp"
#include <bgui.hpp>
#include <filesystem>
#include <functional>
#include <memory>
#include <regex>

namespace editor::ui {
struct markdown_options {
    std::filesystem::path base_directory;
    std::function<void(const std::filesystem::path&)> open_document;
    std::function<void(const std::string&)> open_external = open_desktop_link;
};
inline std::filesystem::path markdown_local_path(const std::string& target, const std::filesystem::path& base) {
    if (target.empty() || target.front() == '#' || target.find(':') != std::string::npos) return {};
    auto path = target.substr(0, target.find_first_of("#?"));
    std::string decoded;
    auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch-'0';
        if (ch >= 'a' && ch <= 'f') return ch-'a'+10;
        if (ch >= 'A' && ch <= 'F') return ch-'A'+10;
        return -1;
    };
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size() && hex(path[i+1]) >= 0 && hex(path[i+2]) >= 0) {
            const char ch = static_cast<char>(hex(path[i+1])*16+hex(path[i+2]));
            if (!ch) return {};
            decoded += ch; i += 2;
        } else {
            if (!path[i]) return {};
            decoded += path[i];
        }
    }
    return (base / std::filesystem::path(decoded)).lexically_normal();
}
inline bool markdown_external_link(const std::string& target) {
    return is_desktop_link(target);
}

class markdown_image final : public bgui::linear {
    bgui::image& m_image;
public:
    explicit markdown_image(const bgui::texture& texture)
        : bgui::linear(bgui::orientation::vertical), m_image(add<bgui::image>(texture)) {
        add_class("markdown-image");
        style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        style.layout.margin = bgui::vec4i{0,8,0,8};
        m_image.style.visual.border_radius = 8.f;
        m_image.style.visual.border_size = 0.f;
        m_image.style.visual.border.normal = bgui::color{1.f,1.f,1.f,1.f};
    }
    void on_update() override {
        const auto size = m_image.get_texture().m_size;
        const float available = processed_width() / bgui::get_global_scale();
        if (available > 0.f && size.x > 0.f && size.y > 0.f) {
            const float width = std::min(available, size.x);
            m_image.set_size(width, std::max(1.f, size.y * width / size.x));
        }
        bgui::linear::on_update();
    }
};

class markdown_text final : public bgui::text {
    struct link_range { std::size_t start, end; std::string target; };
    std::vector<link_range> m_links;
    markdown_options m_options;
public:
    markdown_text(const std::vector<markdown::span>& spans, float scale, markdown_options options)
        : bgui::text("", scale), m_options(std::move(options)) {
        std::string buffer;
        std::size_t codepoints = 0;
        struct highlight { std::size_t byte; std::string text; bgui::vec4 color; };
        std::vector<highlight> highlights;
        for (const auto& span : spans) {
            const auto start = codepoints;
            for (unsigned char ch : span.text) if ((ch & 0xc0) != 0x80) ++codepoints;
            if (span.type == markdown::span_kind::link) m_links.push_back({start, codepoints, span.target});
            if (span.type == markdown::span_kind::link || span.type == markdown::span_kind::code)
                highlights.push_back({buffer.size(), span.text, span.type == markdown::span_kind::link
                    ? bgui::vec4{.40f,.70f,1.f,1.f} : bgui::vec4{.90f,.72f,.45f,1.f}});
            buffer += span.text;
        }
        set_buffer(buffer);
        auto rules = std::make_shared<std::vector<bgui::syntax_highlight_rule>>();
        for (const auto& highlight : highlights) {
            if (highlight.text.empty()) continue;
            std::string literal;
            for (char ch : highlight.text) {
                if (std::string_view(R"(\.^$|()[]{}*+?)").find(ch) != std::string_view::npos) literal += '\\';
                literal += ch;
            }
            const auto pattern = literal + "(?=[\\s\\S]{" +
                std::to_string(buffer.size()-highlight.byte-highlight.text.size()) + "}$)";
            rules->push_back({pattern, std::regex(pattern), highlight.color});
        }
        if (!rules->empty()) set_highlight_rules(rules);
        set_wrap(true);
        receives_input(!m_links.empty());
        style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        style.layout.margin = bgui::vec4i{0,2,0,2};
        style.layout.align = bgui::vec<2,bgui::alignment>{bgui::alignment::start,bgui::alignment::start};
    }
    void on_update() override {
        if (const auto* parent = get_parent()) {
            const auto& padding = parent->computed_style.layout.padding;
            const float available = (parent->processed_width() - padding.x - padding.z) / bgui::get_global_scale();
            if (available > 0.f) set_wrap_width(available);
        }
        bgui::text::on_update();
    }
    std::string link_at(std::size_t codepoint) const {
        for (const auto& link : m_links) if (codepoint >= link.start && codepoint < link.end) return link.target;
        return {};
    }
    void activate_link(const std::string& target) const {
        if (markdown_external_link(target)) {
            if (m_options.open_external) m_options.open_external(target);
        } else if (m_options.open_document) {
            const auto path = markdown_local_path(target, m_options.base_directory);
            if (!path.empty()) m_options.open_document(path);
        }
    }
    void on_released() override {
        const auto mouse = bgui::get_mouse_position();
        const auto target = link_at(get_cursor_position_at(static_cast<float>(mouse.x), static_cast<float>(mouse.y)));
        if (!target.empty()) {
            // Copy the action: navigating can destroy this text and its document tree.
            const auto options = m_options;
            bgui::add_function([options, target] {
                if (markdown_external_link(target)) {
                    if (options.open_external) options.open_external(target);
                } else if (options.open_document) {
                    const auto path = markdown_local_path(target, options.base_directory);
                    if (!path.empty()) options.open_document(path);
                }
            });
        }
    }
    void on_mouse_hover() override {
        const auto mouse = bgui::get_mouse_position();
        if (!link_at(get_cursor_position_at(static_cast<float>(mouse.x), static_cast<float>(mouse.y))).empty())
            bgui::get_context().m_actual_cursor = bgui::cursor::hand;
    }
};

inline std::vector<bgui::element*> render_markdown(bgui::linear& parent, const std::string& source,
                                                 const markdown_options& options = {}) {
    const auto document = markdown::parse(source);
    std::vector<bgui::element*> elements;
    auto add_text = [&](bgui::linear& container, const std::string& text, float scale = .30f) -> markdown_text& {
        return container.add<markdown_text>(markdown::inline_spans(text, document.links), scale, options);
    };
    for (const auto& block : document.blocks) {
        if (block.type == markdown::kind::code) {
            auto& box = parent.add<bgui::linear>(bgui::orientation::vertical);
            elements.push_back(&box); box.add_class("markdown-code");
            box.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            box.style.layout.padding = bgui::vec4i{10};
            box.style.layout.margin = bgui::vec4i{0,6,0,6};
            box.style.visual.background.normal = bgui::color{.09f,.09f,.10f,1.f};
            box.style.visual.border_radius = 6.f; box.style.visual.border_size = 0.f;
            if (!block.language.empty()) {
                auto& language = box.add<bgui::text>(block.language, .25f);
                language.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            }
            auto& code = box.add<markdown_text>(
                std::vector<markdown::span>{{markdown::span_kind::text, block.text.empty() ? " " : block.text, {}}}, .30f, options);
            code.add_class("api-code");
            code.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            code.style.visual.text.normal = bgui::color{.88f,.80f,.64f,1.f};
            code.set_wrap(true);
        } else if (block.type == markdown::kind::table) {
            auto& table = parent.add<bgui::linear>(bgui::orientation::vertical);
            elements.push_back(&table); table.add_class("markdown-table");
            table.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
            table.style.layout.margin = bgui::vec4i{0,6,0,6};
            for (std::size_t r = 0; r < block.rows.size(); ++r) {
                auto& row = table.add<bgui::linear>(bgui::orientation::horizontal);
                row.style.layout.require_mode(bgui::mode::match_parent,bgui::mode::wrap_content);
                row.style.visual.background.normal = r == 0 ? bgui::color{.23f,.25f,.28f,1.f}
                    : bgui::color{r%2 ? .13f : .16f,r%2 ? .13f : .16f,r%2 ? .14f : .17f,1.f};
                for (std::size_t c = 0; c < block.rows[r].size(); ++c) {
                    auto& cell = row.add<bgui::linear>(bgui::orientation::vertical);
                    cell.style.layout.require_mode(bgui::mode::stretch,bgui::mode::wrap_content);
                    cell.style.layout.limit_min = bgui::vec2i{0,0};
                    cell.style.layout.padding = bgui::vec4i{8};
                    cell.style.visual.border_size = 1.f;
                    cell.style.visual.border.normal = bgui::color{.30f,.30f,.32f,1.f};
                    auto& label = add_text(cell, block.rows[r][c]);
                    if (block.alignment[c] >= 0) {
                        label.style.layout.require_width(bgui::mode::wrap_content);
                        label.style.layout.align = bgui::vec<2,bgui::alignment>{block.alignment[c] == 0
                            ? bgui::alignment::center : bgui::alignment::end,bgui::alignment::start};
                    }
                }
            }
        } else {
            auto spans = markdown::inline_spans(block.text, document.links);
            std::vector<markdown::span> text;
            auto flush = [&] {
                if (text.empty()) return;
                bgui::linear* container = &parent;
                if (block.type == markdown::kind::quote) {
                    auto& quote = parent.add<bgui::linear>(bgui::orientation::vertical);
                    quote.add_class("markdown-quote");
                    quote.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
                    quote.style.layout.padding = bgui::vec4i{12,6,12,6};
                    quote.style.visual.background.normal = bgui::color{.16f,.18f,.21f,1.f};
                    elements.push_back(&quote);
                    container = &quote;
                }
                auto& paragraph = container->add<markdown_text>(text,
                    block.level ? .30f + (6-block.level)*.035f : .30f, options);
                if (block.level) {
                    paragraph.style.layout.margin = bgui::vec4i{0,10,0,6};
                    paragraph.style.visual.text.normal = bgui::color{.84f,.84f,.84f,1.f};
                }
                if (container == &parent) elements.push_back(&paragraph);
                text.clear();
            };
            for (const auto& span : spans) {
                if (span.type != markdown::span_kind::image) { text.push_back(span); continue; }
                flush();
                try {
                    const auto path = markdown_local_path(span.target, options.base_directory);
                    if (path.empty()) throw std::runtime_error("Use uma imagem local.");
                    const auto& texture = bgui::asset_manager::get_instance().load_texture(std::filesystem::absolute(path).string());
                    auto& image = parent.add<markdown_image>(texture);
                    image.set_tooltip(span.text);
                    elements.push_back(&image);
                } catch (const std::exception&) {
                    auto& error = add_text(parent, "Imagem indisponível: " + span.text + " (" + span.target + ")");
                    error.add_class("markdown-image-error"); elements.push_back(&error);
                }
            }
            flush();
            if (spans.empty()) {
                auto& space = add_text(parent, " "); elements.push_back(&space);
            }
        }
    }
    return elements;
}
}
