#pragma once

#include <elem/text.hpp>
#include <lay/linear.hpp>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace editor::ui {
inline std::vector<bgui::element*> render_markdown(bgui::linear& parent, const std::string& markdown) {
    std::vector<bgui::element*> paragraphs;
    std::istringstream lines(markdown);
    std::string line;
    bool in_code_block = false;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string display = line;
        int heading = 0;
        while (heading < static_cast<int>(line.size()) && heading < 6 && line[heading] == '#') ++heading;
        if (line.rfind("```", 0) == 0 || line.rfind("~~~", 0) == 0) {
            in_code_block = !in_code_block;
            continue;
        }
        if (in_code_block) {
            heading = 0;
            display = "    " + line;
        } else if (heading && heading < static_cast<int>(line.size()) && line[heading] == ' ') {
            display = line.substr(static_cast<size_t>(heading + 1));
        } else if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
            display = "•  " + line.substr(2);
        } else if (line.rfind("> ", 0) == 0) {
            display = "│  " + line.substr(2);
        } else if (line.size() > 2 && std::isdigit(static_cast<unsigned char>(line[0]))) {
            const auto marker = line.find(". ");
            if (marker != std::string::npos) display = "•  " + line.substr(marker + 2);
        } else if (line == "---" || line == "***" || line == "___") {
            display = "────────────────────────────────────────";
        }
        if (!in_code_block) {
            display = std::regex_replace(display, std::regex(R"(\[([^\]]+)\]\(([^)]+)\))"), "$1 ($2)");
            display = std::regex_replace(display, std::regex(R"(`([^`]+)`|\*\*([^*]+)\*\*|__([^_]+)__|\*([^*]+)\*|_([^_]+)_)"), "$1$2$3$4$5");
            display = std::regex_replace(display, std::regex(R"(~~([^~]+)~~)"), "$1");
        }
        auto& paragraph = parent.add<bgui::text>(display, heading ? .30f + (6 - heading) * 0.035f : .30f);
        paragraph.set_wrap(true);
        paragraph.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        paragraph.style.layout.margin = bgui::vec4i{0, heading ? 10 : 2, 0, heading ? 6 : 2};
        paragraph.style.layout.align = bgui::vec<2, bgui::alignment>{bgui::alignment::start, bgui::alignment::start};
        if (in_code_block) paragraph.add_class("api-code");
        if (heading) paragraph.style.visual.text.normal = bgui::color{0.84f, 0.84f, 0.84f, 1.f};
        paragraphs.push_back(&paragraph);
    }
    return paragraphs;
}
}
