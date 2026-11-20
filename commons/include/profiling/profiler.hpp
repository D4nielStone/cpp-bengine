#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <utility>
#include <filesystem>

#ifndef BENGINE_PROFILING
#define BENGINE_PROFILING 1
#endif
namespace Bubble {
struct profile_event {
    std::string name, category;
    std::uint64_t thread{}, frame{}, id{}, parent{};
    double start_us{}, total_us{}, self_us{};
};
struct profile_frame {
    std::uint64_t id{}, dropped{}, draw_calls{}, triangles{}, vertices{};
    double ms{}, gpu_ms{-1};
    std::vector<profile_event> events;
    std::vector<std::pair<std::string, double>> gpu_passes;
};
struct profile_statistics {
    double mean{}, median{}, p95{}, p99{}, minimum{}, maximum{};
};
profile_statistics summarize_frames(const std::vector<profile_frame>& frames);
class profiler {
public:
    static profiler& instance();
    void next_frame(double seconds);
    void record(profile_event event) noexcept;
    void draw(std::uint64_t vertices, std::uint64_t triangles) noexcept;
    void gpu(std::uint64_t frame, double ms, const char* pass = "Render");
    std::vector<profile_frame> snapshot() const;
    bool export_capture(const std::filesystem::path&, bool chrome, std::string& error) const;
    static bool export_frames(const std::vector<profile_frame>&, const std::filesystem::path&, bool chrome, std::string& error);
    std::atomic<bool> enabled{BENGINE_PROFILING != 0};
    std::atomic<std::uint64_t> frame_id{0};
private:
    mutable std::mutex mutex_;
    std::deque<profile_frame> frames_;
    profile_frame current_;
};
class profile_scope {
public:
    profile_scope(const char* name, const char* category) noexcept;
    ~profile_scope();
    profile_scope(const profile_scope&) = delete;
    profile_scope& operator=(const profile_scope&) = delete;
private:
    profile_scope* parent_{};
    profile_event event_;
    std::chrono::steady_clock::time_point start_;
    double children_{};
    bool active_{};
    static thread_local profile_scope* top_;
};
}
#define BUBBLE_PROFILE_JOIN_(a,b) a##b
#define BUBBLE_PROFILE_JOIN(a,b) BUBBLE_PROFILE_JOIN_(a,b)
#if BENGINE_PROFILING
#define BUBBLE_PROFILE(name,category) ::Bubble::profile_scope BUBBLE_PROFILE_JOIN(bubble_scope_,__LINE__)(name,category)
#else
#define BUBBLE_PROFILE(name,category) ((void)0)
#endif
