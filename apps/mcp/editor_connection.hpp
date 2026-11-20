#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace bubble::mcp {

constexpr std::size_t live_message_limit = 8 * 1024 * 1024;
unsigned editor_port(std::string_view value);
std::string editor_token();
std::string call_editor(unsigned port, const std::string& request);

class editor_connection {
public:
    using handler = std::function<std::string(std::string_view)>;
    editor_connection(unsigned port, handler dispatch);
    ~editor_connection();
    void poll();

private:
    struct implementation;
    std::unique_ptr<implementation> m_impl;
};

}
