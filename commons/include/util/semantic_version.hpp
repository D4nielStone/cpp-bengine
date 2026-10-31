#pragma once
#include <array>
#include <charconv>
#include <compare>
#include <optional>
#include <string_view>

namespace commons {
struct semantic_version {
    std::array<unsigned, 3> parts{};
    auto operator<=>(const semantic_version&) const = default;
    static std::optional<semantic_version> parse(std::string_view text) {
        semantic_version result;
        for (unsigned index = 0; index < 3; ++index) {
            const auto end = text.find('.');
            const auto part = text.substr(0, end);
            if (part.empty() || (part.size() > 1 && part.front() == '0')) return {};
            const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result.parts[index]);
            if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size()) return {};
            if (index == 2) { if (end != text.npos) return {}; }
            else { if (end == text.npos) return {}; text.remove_prefix(end + 1); }
        }
        return result;
    }
};
}
