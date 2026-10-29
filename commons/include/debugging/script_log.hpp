#pragma once

#include <cstdint>
#include <deque>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

namespace commons {

// Shared by script states in this process; the engine runs scripts on its UI thread.
class script_log {
public:
    static constexpr std::size_t capacity = 1000;
    static script_log& instance() { static script_log value; return value; }
    void write(std::uint32_t entity, const std::string& message) {
        auto line = "[lua][" + std::to_string(entity) + "] " + message.substr(0, 65536);
        std::clog << line << '\n';
        if (m_output.is_open()) {
            const auto size = static_cast<std::uint32_t>(line.size());
            m_output.write(reinterpret_cast<const char*>(&size), sizeof(size));
            m_output.write(line.data(), size);
            m_output.flush();
        }
        append(line);
    }
    void set_output_file(const std::filesystem::path& path) {
        m_output = std::ofstream(path, std::ios::binary | std::ios::trunc);
    }
    void append(std::string line) {
        if (m_lines.size() == capacity) m_lines.pop_front();
        m_lines.push_back(std::move(line));
        ++m_revision;
    }
    void clear() { m_lines.clear(); ++m_revision; }
    const std::deque<std::string>& lines() const { return m_lines; }
    std::size_t revision() const { return m_revision; }
private:
    std::ofstream m_output;
    std::deque<std::string> m_lines;
    std::size_t m_revision{0};
};

// Read only complete records; a concurrent writer can still be flushing the last one.
inline void read_script_log(const std::filesystem::path& path, std::uintmax_t& offset) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return;
    input.seekg(static_cast<std::streamoff>(offset));
    std::uint32_t size;
    while (input.read(reinterpret_cast<char*>(&size), sizeof(size))) {
        if (size > 66000) return;
        std::string line(size, '\0');
        if (!input.read(line.data(), size)) return;
        script_log::instance().append(std::move(line));
        offset += sizeof(size) + size;
    }
}

}
