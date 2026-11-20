#include "editor_connection.hpp"

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bubble::mcp {
namespace {
using clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

#ifdef _WIN32
using handle = SOCKET;
constexpr handle invalid = INVALID_SOCKET;
#else
using handle = int;
constexpr handle invalid = -1;
#endif

void initialize() {
#ifdef _WIN32
    static const auto ready = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready) throw std::runtime_error("Could not initialize editor sockets.");
#endif
}

struct connection {
    handle fd{invalid};
    connection() = default;
    explicit connection(handle value) : fd(value) {}
    connection(const connection&) = delete;
    connection& operator=(connection&& other) noexcept {
        reset();
        fd = std::exchange(other.fd, invalid);
        return *this;
    }
    ~connection() { reset(); }
    void reset() {
        if (fd == invalid) return;
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
        fd = invalid;
    }
    bool valid() const { return fd != invalid; }
    bool nonblocking() const {
#ifdef _WIN32
        u_long mode = 1;
        return ioctlsocket(fd, FIONBIO, &mode) == 0;
#else
        return fcntl(fd, F_SETFL, O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0;
#endif
    }
};

bool pending() {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR || errno == EINPROGRESS;
#endif
}

sockaddr_in address(unsigned port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(static_cast<unsigned short>(port));
    result.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return result;
}

bool send_bytes(const connection& socket, const std::string& data, std::size_t& sent) {
    if (sent == data.size()) return true;
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    const auto count = send(socket.fd, data.data() + sent,
        static_cast<int>(std::min<std::size_t>(data.size() - sent, 65536)), flags);
    if (count <= 0) return count < 0 && pending();
    sent += count;
    return true;
}

bool receive_bytes(const connection& socket, std::string& data) {
    char bytes[32768];
    const auto count = recv(socket.fd, bytes, sizeof(bytes), 0);
    if (count <= 0) return count < 0 && pending();
    data.append(bytes, count);
    return data.size() <= live_message_limit;
}

std::string frame(std::string data) {
    if (data.size() >= live_message_limit || data.find('\n') != std::string::npos)
        throw std::runtime_error("Invalid live editor message size or framing.");
    data += '\n';
    return data;
}

}

unsigned editor_port(std::string_view value) {
    unsigned port{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() || port == 0 || port > 65535)
        throw std::runtime_error("Expected an editor port between 1 and 65535.");
    return port;
}

std::string editor_token() {
    const auto* value = std::getenv("BENGINE_EDITOR_MCP_TOKEN");
    const std::string token = value ? value : "";
    if (token.size() < 32 || token.size() > 256 ||
        std::ranges::any_of(token, [](unsigned char c) { return c < 33 || c > 126; }))
        throw std::runtime_error("Set BENGINE_EDITOR_MCP_TOKEN to a shared random ASCII token of 32–256 characters.");
    return token;
}

struct editor_connection::implementation {
    connection listener, client;
    handler dispatch;
    std::string request, response;
    std::size_t sent{};
    clock::time_point deadline;

    implementation(unsigned port, handler callback) : listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)),
        dispatch(std::move(callback)) {
        const auto local = address(port);
        if (!listener.valid() || !listener.nonblocking() ||
            bind(listener.fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 ||
            listen(listener.fd, 4) != 0)
            throw std::runtime_error("Could not bind the live editor port.");
    }

    void poll() {
        if (!client.valid()) {
            client = connection(accept(listener.fd, nullptr, nullptr));
            if (!client.valid()) return;
            if (!client.nonblocking()) { client.reset(); return; }
            request.clear();
            response.clear();
            sent = 0;
            deadline = clock::now() + 15s;
        }
        if (clock::now() >= deadline) { client.reset(); return; }
        if (response.empty()) {
            for (int batch = 0; batch < 16; ++batch) {
                const auto size = request.size();
                if (!receive_bytes(client, request)) { client.reset(); return; }
                if (!request.empty() && request.back() == '\n') {
                    try { response = frame(dispatch(std::string_view(request).substr(0, request.size() - 1))); }
                    catch (...) { response = "{\"error\":\"Live editor request failed.\"}\n"; }
                    break;
                }
                if (request.size() == size) return;
            }
        }
        if (response.empty()) return;
        for (int batch = 0; batch < 16; ++batch) {
            const auto size = sent;
            if (!send_bytes(client, response, sent) || sent == response.size()) { client.reset(); return; }
            if (sent == size) return;
        }
    }
};

editor_connection::editor_connection(unsigned port, handler dispatch) {
    initialize();
    m_impl = std::make_unique<implementation>(port, std::move(dispatch));
}
editor_connection::~editor_connection() = default;
void editor_connection::poll() { m_impl->poll(); }

std::string call_editor(unsigned port, const std::string& request) {
    initialize();
    connection client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    const auto local = address(port);
    if (!client.valid() || !client.nonblocking() ||
        (connect(client.fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0 && !pending()))
        throw std::runtime_error("The live editor port is unavailable.");
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(client.fd, &writable);
    timeval timeout{2, 0};
    if (select(static_cast<int>(client.fd) + 1, nullptr, &writable, nullptr, &timeout) <= 0)
        throw std::runtime_error("The live editor port did not answer.");
    const auto data = frame(request);
    const auto deadline = clock::now() + 15s;
    std::size_t sent{};
    std::string response;
    while (clock::now() < deadline) {
        const auto progress = sent == data.size() ? response.size() : sent;
        if (sent != data.size()) {
            if (!send_bytes(client, data, sent)) break;
            if (sent != progress) continue;
        } else {
            if (!receive_bytes(client, response)) break;
            if (!response.empty() && response.back() == '\n') {
                response.pop_back();
                return response;
            }
            if (response.size() != progress) continue;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("Live editor connection closed or timed out.");
}

}
