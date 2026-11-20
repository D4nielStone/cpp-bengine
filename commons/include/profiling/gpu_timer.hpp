#pragma once
#include <array>
#include <cstdint>
namespace Bubble {
// Owned by a renderer; destroy with its OpenGL context current.
class gpu_timer {
public:
    explicit gpu_timer(const char* pass = "Render") : pass_(pass) {}
    ~gpu_timer();
    gpu_timer(const gpu_timer&) = delete;
    gpu_timer& operator=(const gpu_timer&) = delete;
    bool begin();
    void end();
private:
    struct sample { unsigned query[2]{}; std::uint64_t frame{}; bool pending{}; };
    std::array<sample, 8> samples_{};
    sample* active_{};
    const char* pass_;
    void* context_{};
    using function = void (*)();
    function query_counter_{}, query_result_{};
};
class gpu_scope {
public:
    explicit gpu_scope(gpu_timer& timer) : timer_(timer), active_(timer_.begin()) {}
    ~gpu_scope() { if (active_) timer_.end(); }
    gpu_scope(const gpu_scope&) = delete;
    gpu_scope& operator=(const gpu_scope&) = delete;
private:
    gpu_timer& timer_;
    bool active_;
};
}
