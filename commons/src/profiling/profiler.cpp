#include "profiling/profiler.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <numeric>
#include <string_view>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
namespace Bubble {
namespace { std::atomic<std::uint64_t> sequence{1}; }
thread_local profile_scope* profile_scope::top_ = nullptr;
profiler& profiler::instance() { static profiler value; return value; }
profile_statistics summarize_frames(const std::vector<profile_frame>& frames) {
    std::vector<double> values;
    for (const auto& f : frames) if (std::isfinite(f.ms) && f.ms > 0) values.push_back(f.ms);
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    auto percentile = [&](double p) { return values[static_cast<std::size_t>(std::ceil(p * values.size())) - 1]; };
    const auto n = values.size();
    return {std::accumulate(values.begin(), values.end(), 0.0) / n,
        (values[(n-1)/2]+values[n/2])/2, percentile(.95), percentile(.99), values.front(), values.back()};
}
void profiler::next_frame(double seconds) {
#if BENGINE_PROFILING
    std::lock_guard lock(mutex_);
    if ((current_.id || !current_.events.empty()) && enabled.load(std::memory_order_relaxed)) {
        current_.ms = std::isfinite(seconds) && seconds > 0 ? seconds * 1000 : 0;
        frames_.push_back(std::move(current_));
        if (frames_.size() > 240) frames_.pop_front();
    }
    current_ = {};
    current_.id = ++frame_id;
#endif
}
void profiler::record(profile_event event) noexcept {
    try {
        std::lock_guard lock(mutex_);
        if (!enabled.load(std::memory_order_relaxed)) return;
        profile_frame* target = event.frame == current_.id ? &current_ : nullptr;
        if (!target) for (auto& frame : frames_) if (frame.id == event.frame) { target = &frame; break; }
        if (!target) return;
        if (target->events.size() < 4096) target->events.push_back(std::move(event));
        else ++target->dropped;
    } catch (...) { }
}
void profiler::draw(std::uint64_t vertices, std::uint64_t triangles) noexcept {
#if BENGINE_PROFILING
    if (!enabled.load(std::memory_order_relaxed)) return;
    std::lock_guard lock(mutex_);
    ++current_.draw_calls; current_.vertices += vertices; current_.triangles += triangles;
#endif
}
void profiler::gpu(std::uint64_t frame, double ms, const char* pass) {
    if (!std::isfinite(ms) || ms < 0 || !pass) return;
    std::lock_guard lock(mutex_);
    auto add = [ms, pass](profile_frame& f) {
        if (std::string_view(pass) == "Render") { if (f.gpu_ms < 0) f.gpu_ms = 0; f.gpu_ms += ms; }
        for (auto& [name, time] : f.gpu_passes) if (name == pass) { time += ms; return; }
        f.gpu_passes.emplace_back(pass, ms);
    };
    if (current_.id == frame) add(current_);
    else for (auto& f : frames_) if (f.id == frame) { add(f); break; }
}
std::vector<profile_frame> profiler::snapshot() const {
    std::lock_guard lock(mutex_); return {frames_.begin(), frames_.end()};
}
profile_scope::profile_scope(const char* name, const char* category) noexcept {
    if (!profiler::instance().enabled.load(std::memory_order_relaxed)) return;
    try {
        event_.name = std::string_view(name ? name : "").substr(0, 256);
        event_.category = std::string_view(category ? category : "").substr(0, 64);
        event_.id = sequence.fetch_add(1, std::memory_order_relaxed);
        event_.frame = profiler::instance().frame_id.load(std::memory_order_relaxed);
        static thread_local const auto thread_id = sequence.fetch_add(1, std::memory_order_relaxed);
        event_.thread = thread_id;
        parent_ = top_; event_.parent = parent_ ? parent_->event_.id : 0;
        start_ = std::chrono::steady_clock::now();
        event_.start_us = std::chrono::duration<double, std::micro>(start_.time_since_epoch()).count();
        active_ = true; top_ = this;
    } catch (...) { }
}
profile_scope::~profile_scope() {
    if (!active_) return;
    event_.total_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now()-start_).count();
    event_.self_us = std::max(0.0, event_.total_us-children_);
    top_ = parent_;
    if (parent_) parent_->children_ += event_.total_us;
    profiler::instance().record(std::move(event_));
}
bool profiler::export_capture(const std::filesystem::path& path, bool chrome, std::string& error) const {
    return export_frames(snapshot(), path, chrome, error);
}
bool profiler::export_frames(const std::vector<profile_frame>& frames, const std::filesystem::path& path, bool chrome, std::string& error) {
    error.clear();
    try {
        rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
        w.StartObject(); w.Key("version"); w.Int(1);
        w.Key(chrome ? "traceEvents" : "frames"); w.StartArray();
        for (const auto& f : frames) {
            if (!chrome) {
                w.StartObject(); w.Key("id"); w.Uint64(f.id); w.Key("frametime_ms"); w.Double(f.ms);
                w.Key("gpu_ms"); if(f.gpu_ms < 0) w.Null(); else w.Double(f.gpu_ms);
                w.Key("draw_calls_mesh"); w.Uint64(f.draw_calls); w.Key("triangles_submitted"); w.Uint64(f.triangles);
                w.Key("vertices_submitted"); w.Uint64(f.vertices); w.Key("dropped_events"); w.Uint64(f.dropped);
                w.Key("gpu_passes_ms"); w.StartObject();
                for (const auto& [name, ms] : f.gpu_passes) { w.Key(name.c_str()); w.Double(ms); }
                w.EndObject();
                w.Key("events"); w.StartArray();
            }
            for (const auto& e : f.events) {
                w.StartObject(); w.Key("name"); w.String(e.name.c_str()); w.Key("cat"); w.String(e.category.c_str());
                w.Key("ph"); w.String("X"); w.Key("pid"); w.Int(1); w.Key("tid"); w.Uint64(e.thread);
                w.Key("ts"); w.Double(e.start_us); w.Key("dur"); w.Double(e.total_us);
                w.Key("args"); w.StartObject(); w.Key("frame"); w.Uint64(e.frame); w.Key("id"); w.Uint64(e.id);
                w.Key("parent"); w.Uint64(e.parent); w.Key("self_us"); w.Double(e.self_us); w.EndObject(); w.EndObject();
            }
            if (!chrome) { w.EndArray(); w.EndObject(); }
        }
        w.EndArray(); w.EndObject();
        // Exclusive creation also protects against another exporter choosing this path.
#ifdef _WIN32
        auto* out = _wfopen(path.c_str(), L"wbx");
#else
        auto* out = std::fopen(path.c_str(), "wbx");
#endif
        if (!out) { error = std::string("Cannot create capture: ") + std::strerror(errno); return false; }
        const bool written = std::fwrite(buffer.GetString(), 1, buffer.GetSize(), out) == buffer.GetSize();
        const bool closed = std::fclose(out) == 0;
        if (!written || !closed) {
            error = "Cannot write capture";
            std::error_code ignored; std::filesystem::remove(path, ignored);
            return false;
        }
        return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
}
