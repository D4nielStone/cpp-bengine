#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace editor::ui::markdown {
enum class kind { paragraph, heading, quote, code, table };
enum class span_kind { text, code, link, image };
struct span {
    span_kind type = span_kind::text;
    std::string text;
    std::string target;
};
struct block {
    kind type = kind::paragraph;
    std::string text;
    int level = 0;
    std::vector<std::vector<std::string>> rows;
    std::vector<int> alignment;
    std::string language;
};
using references = std::map<std::string, std::string>;
struct document {
    std::vector<block> blocks;
    references links;
};
inline std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
inline std::string reference_key(const std::string& text) {
    std::string result;
    bool space = false;
    for (unsigned char ch : trim(text)) {
        if (std::isspace(ch)) { space = !result.empty(); continue; }
        if (space) result += ' ';
        space = false;
        result += static_cast<char>(std::tolower(ch));
    }
    return result;
}
inline std::string destination(std::string text) {
    text = trim(text);
    if (!text.empty() && text.front() == '<') {
        const auto end = text.find('>');
        return end == std::string::npos ? std::string{} : text.substr(1, end - 1);
    }
    const auto title = text.find_first_of(" \t");
    return text.substr(0, title);
}
inline std::vector<std::string> cells(const std::string& line) {
    auto text = trim(line);
    if (!text.empty() && text.front() == '|') text.erase(0, 1);
    if (!text.empty() && text.back() == '|' && (text.size() < 2 || text[text.size()-2] != '\\')) text.pop_back();
    std::vector<std::string> result;
    std::string cell;
    std::size_t code_ticks = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            cell += text[i]; cell += text[++i];
        } else if (text[i] == '`') {
            std::size_t count = 1;
            while (i + count < text.size() && text[i + count] == '`') ++count;
            if (!code_ticks) code_ticks = count;
            else if (code_ticks == count) code_ticks = 0;
            cell.append(count, '`'); i += count - 1;
        } else if (text[i] == '|' && !code_ticks) {
            result.push_back(trim(cell)); cell.clear();
        } else cell += text[i];
    }
    result.push_back(trim(cell));
    return result;
}
inline std::vector<int> table_alignment(const std::string& text) {
    if (text.find('|') == std::string::npos) return {};
    std::vector<int> result;
    for (auto cell : cells(text)) {
        const bool left = !cell.empty() && cell.front() == ':';
        const bool right = !cell.empty() && cell.back() == ':';
        if (left) cell.erase(0, 1);
        if (right && !cell.empty()) cell.pop_back();
        if (cell.size() < 3 || cell.find_first_not_of('-') != std::string::npos) return {};
        result.push_back(left && right ? 0 : right ? 1 : -1);
    }
    return result;
}
inline document parse(const std::string& source) {
    std::vector<std::string> lines;
    for (std::size_t start = 0; start < source.size();) {
        const auto end = source.find('\n', start);
        auto line = source.substr(start, end == std::string::npos ? end : end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    document result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        auto line = lines[i];
        auto stripped = trim(line);
        if (stripped.starts_with("```") || stripped.starts_with("~~~")) {
            const char marker = stripped.front();
            const auto count = stripped.find_first_not_of(marker);
            const auto length = count == std::string::npos ? stripped.size() : count;
            block code{kind::code, {}, 0, {}, {}};
            code.language = trim(stripped.substr(length));
            for (++i; i < lines.size(); ++i) {
                const auto close = trim(lines[i]);
                const auto end = close.find_first_not_of(marker);
                if (!close.empty() && close.front() == marker &&
                    (end == std::string::npos ? close.size() : end) >= length &&
                    (end == std::string::npos || trim(close.substr(end)).empty())) break;
                code.text += lines[i] + '\n';
            }
            if (!code.text.empty()) code.text.pop_back();
            result.blocks.push_back(std::move(code));
            continue;
        }
        if (stripped.starts_with('[')) {
            const auto end = stripped.find("]:");
            if (end != std::string::npos) {
                auto target = destination(stripped.substr(end + 2));
                if (!target.empty()) {
                    result.links.emplace(reference_key(stripped.substr(1, end - 1)), std::move(target));
                    continue;
                }
            }
        }
        if (i + 1 < lines.size() && line.find('|') != std::string::npos) {
            auto alignment = table_alignment(lines[i + 1]);
            auto header = cells(line);
            if (!alignment.empty() && alignment.size() == header.size()) {
                block table{kind::table, {}, 0, {std::move(header)}, std::move(alignment)};
                ++i;
                while (i + 1 < lines.size() && !trim(lines[i+1]).empty() && lines[i+1].find('|') != std::string::npos) {
                    auto row = cells(lines[++i]);
                    row.resize(table.alignment.size());
                    table.rows.push_back(std::move(row));
                }
                result.blocks.push_back(std::move(table));
                continue;
            }
        }
        block value{kind::paragraph, line, 0, {}, {}};
        while (value.level < static_cast<int>(stripped.size()) && value.level < 6 && stripped[value.level] == '#') ++value.level;
        if (value.level && stripped.size() > static_cast<std::size_t>(value.level) && stripped[value.level] == ' ') {
            value.type = kind::heading; value.text = stripped.substr(value.level + 1);
        } else {
            value.level = 0;
            if (stripped.starts_with('>')) { value.type = kind::quote; value.text = trim(stripped.substr(1)); }
            else if (stripped.starts_with("- ") || stripped.starts_with("* ")) value.text = "•  " + stripped.substr(2);
            else if (stripped == "---" || stripped == "***" || stripped == "___") value.text = "────────────────────";
            else if (!stripped.empty() && std::isdigit(static_cast<unsigned char>(stripped.front()))) {
                const auto end = stripped.find_first_not_of("0123456789");
                if (end != std::string::npos && stripped.substr(end, 2) == ". ") value.text = stripped;
            }
        }
        result.blocks.push_back(std::move(value));
    }
    return result;
}
inline std::vector<span> inline_spans(const std::string& text, const references& links = {}) {
    std::vector<span> result;
    std::string plain;
    auto flush = [&] { if (!plain.empty()) { result.push_back({span_kind::text, std::move(plain), {}}); plain.clear(); } };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\\' && i + 1 < text.size() && std::ispunct(static_cast<unsigned char>(text[i+1]))) {
            plain += text[i+1]; i += 2; continue;
        }
        if (text[i] == '`') {
            std::size_t count = 1;
            while (i + count < text.size() && text[i + count] == '`') ++count;
            const auto end = text.find(std::string(count, '`'), i + count);
            if (end != std::string::npos) {
                flush(); result.push_back({span_kind::code, text.substr(i + count, end - i - count), {}});
                i = end + count; continue;
            }
        }
        const bool image = text.compare(i, 2, "![") == 0;
        const auto open = i + (image ? 1 : 0);
        if (text[open] == '[') {
            const auto close = text.find(']', open + 1);
            if (close != std::string::npos) {
                const auto label = text.substr(open + 1, close - open - 1);
                std::string target;
                auto next = close + 1;
                if (next < text.size() && text[next] == '(') {
                    std::size_t end = next + 1;
                    int depth = 1;
                    for (; end < text.size(); ++end) {
                        if (text[end] == '\\' && end + 1 < text.size()) { ++end; continue; }
                        if (text[end] == '(') ++depth;
                        if (text[end] == ')' && --depth == 0) break;
                    }
                    if (end < text.size()) { target = destination(text.substr(next + 1, end - next - 1)); next = end + 1; }
                } else {
                    auto key = label;
                    if (next < text.size() && text[next] == '[') {
                        const auto end = text.find(']', next + 1);
                        if (end != std::string::npos) {
                            if (end > next + 1) key = text.substr(next + 1, end - next - 1);
                            next = end + 1;
                        }
                    }
                    if (const auto ref = links.find(reference_key(key)); ref != links.end()) target = ref->second;
                }
                if (!target.empty()) {
                    flush(); result.push_back({image ? span_kind::image : span_kind::link, label, target}); i = next; continue;
                }
            }
        }
        if (text[i] == '<') {
            const auto end = text.find('>', i + 1);
            if (end != std::string::npos) {
                const auto target = text.substr(i+1, end-i-1);
                if (target.starts_with("https://") || target.starts_with("http://") || target.starts_with("mailto:")) {
                    flush(); result.push_back({span_kind::link, target, target}); i = end+1; continue;
                }
            }
        }
        if (text[i] == '*' || text[i] == '_' || text.compare(i, 2, "~~") == 0) {
            const auto marker = text.substr(i, text.compare(i, 2, "~~") == 0 || (i+1 < text.size() && text[i+1] == text[i]) ? 2 : 1);
            const auto end = text.find(marker, i+marker.size());
            const bool intraword = text[i] == '_' && i && std::isalnum(static_cast<unsigned char>(text[i-1]));
            if (!intraword && end != std::string::npos && end > i+marker.size()) {
                flush();
                auto nested = inline_spans(text.substr(i+marker.size(), end-i-marker.size()), links);
                result.insert(result.end(), nested.begin(), nested.end());
                i = end+marker.size(); continue;
            }
        }
        plain += text[i++];
    }
    flush(); return result;
}
}
