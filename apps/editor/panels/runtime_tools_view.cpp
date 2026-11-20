#include "../core/localization.hpp"
#include "runtime_tools_view.hpp"
#include <elem/button.hpp>
#include <elem/inputbox.hpp>
#include <lay/linear.hpp>
#include <fstream>
#include <map>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#if defined(__linux__)
#include <unistd.h>
#endif
namespace editor::ui {
namespace {
class profiler_graph final : public bgui::element {
public:
    const std::vector<Bubble::profile_frame>* frames{};
    bool supports_render_cache() const override { return false; }
    void get_requires(bgui::draw_data* data) override {
        bgui::element::get_requires(data);
        if (!frames || frames->size() < 2) return;
        auto& draw = data->m_draw_list;
        const auto color = draw.get_color();
        const float x = processed_x(), y = processed_y(), w = processed_width(), h = processed_height();
        double maximum = 33.333;
        for (const auto& f : *frames) maximum = std::max(maximum, f.ms);
        auto point = [&](std::size_t i) { return bgui::vec2{x+w*float(i)/float(frames->size()-1), y+h-h*float((*frames)[i].ms/maximum)}; };
        draw.set_color({.4f,.8f,.5f,1});
        for(std::size_t i=1;i<frames->size();++i) draw.add_line(point(i-1), point(i), 1.5f);
        draw.set_color({.9f,.4f,.3f,1});
        const float threshold = y+h-h*float(33.333/maximum);
        draw.add_line({x,threshold},{x+w,threshold});
        draw.set_color(color);
    }
};
class profiler_layout final : public bgui::linear {
public:
    profiler_layout() : bgui::linear(bgui::orientation::vertical) { set_scrollable(true); }
    std::function<void()> refresh;
    void on_update() override { if(refresh) refresh(); bgui::linear::on_update(); }
};
}
runtime_tools_view::runtime_tools_view(commons::window&, std::shared_ptr<editor_context> context,
    std::shared_ptr<commons::render_system> renderer) : m_context(std::move(context)) {
    if (!m_context || !m_context->registry || !renderer) throw std::invalid_argument("Profiler requires editor context and renderer");
}
void runtime_tools_view::build(bgui::dock& dock) {
    auto& window = dock.add_window(tr("Profiler"), bgui::dock_area::right, .30f);
    window.add_class("editor-panel");
    auto& content = bgui::panel_content(window).add<profiler_layout>();
    content.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::match_parent);
    content.refresh = [this] { update(); };
    content.add<bgui::button>("Pause / resume capture", .30f, [this] {
        m_paused = !m_paused; Bubble::profiler::instance().enabled = !m_paused && BENGINE_PROFILING;
    });
    content.add<bgui::button>("Previous frame", .30f, [this] { if(m_selected) { m_paused = true; Bubble::profiler::instance().enabled = false; --m_selected; } });
    content.add<bgui::button>("Next frame", .30f, [this] { if(m_selected+1 < m_capture.size()) { m_paused = true; Bubble::profiler::instance().enabled = false; ++m_selected; } });
    content.add<bgui::inputbox>("", "Filter name/category/thread", .30f,
        [this](const std::string& value) { m_filter = value.substr(0, 256); }, bgui::input_mode::inputbox);
    content.add<bgui::button>("Set comparison baseline", .30f, [this] { m_baseline = m_capture; });
    for (bool chrome : {false, true}) content.add<bgui::button>(chrome ? "Export Chrome Trace" : "Export JSON", .30f, [this, chrome] {
        if (m_context->project_root.empty()) { m_status = "Open a saved project before exporting"; return; }
        const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto path = m_context->project_root / ("profiler-" + std::to_string(stamp) + (chrome ? ".trace.json" : ".json"));
        std::string error;
        m_status = Bubble::profiler::export_frames(m_capture, path, chrome, error) ? path.string() : error;
    });
    auto& graph = content.add<profiler_graph>();
    graph.frames = &m_capture;
    graph.style.layout.require_mode(bgui::mode::match_parent, bgui::mode::pixel);
    graph.style.layout.require_height(bgui::mode::pixel, 80.f);
    m_summary = &content.add<bgui::text>("Waiting for frames", .30f);
    m_details = &content.add<bgui::text>("", .30f);
}
void runtime_tools_view::update() {
    if (!m_summary || !m_details) return;
    const auto now = std::chrono::steady_clock::now();
    if (now-m_refresh < std::chrono::milliseconds(200)) return;
    m_refresh = now;
    BUBBLE_PROFILE("Profiler panel", "Editor");
    if (!m_paused) { m_capture = Bubble::profiler::instance().snapshot(); if(!m_capture.empty()) m_selected = m_capture.size()-1; }
    if (m_capture.empty()) { m_summary->set_buffer(BENGINE_PROFILING ? "Waiting for frames" : "Profiling disabled at build time"); return; }
    m_selected = std::min(m_selected, m_capture.size()-1);
    const auto stats = Bubble::summarize_frames(m_capture);
    const auto& frame = m_capture[m_selected];
    std::ostringstream out; out << std::fixed << std::setprecision(2);
    out << (m_context->is_playing ? "Play" : "Editor") << (m_paused ? " | Frozen" : " | Live")
        << " | Frame " << frame.id << "\nFrametime " << frame.ms << " ms | FPS " << (frame.ms > 0 ? 1000/frame.ms : 0)
        << "\nMean / median / P95 / P99 (ms): " << stats.mean << " / " << stats.median << " / " << stats.p95 << " / " << stats.p99
        << "\nFPS avg / min / max: " << (stats.mean > 0 ? 1000/stats.mean : 0) << " / " << (stats.maximum > 0 ? 1000/stats.maximum : 0) << " / " << (stats.minimum > 0 ? 1000/stats.minimum : 0)
        << "\nGPU render intervals: ";
    if(frame.gpu_ms < 0) out << "unavailable / pending"; else out << frame.gpu_ms << " ms";
    for (const auto& [pass, ms] : frame.gpu_passes) if (pass != "Render") out << "\nGPU " << pass << ": " << ms << " ms";
    out << "\nMesh draws: " << frame.draw_calls << " | Submitted triangles: " << frame.triangles << " | Index references: " << frame.vertices;
    std::size_t components = 0; for(const auto& [id, list] : m_context->registry->entities) { (void)id; components += list.size(); }
    out << "\nECS entities (live): " << m_context->registry->entities.size() << " | Components: " << components;
#if defined(__linux__)
    std::ifstream memory("/proc/self/statm"); std::uint64_t pages{}, resident{};
    if(memory >> pages >> resident) out << "\nProcess RSS (live): " << resident * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024*1024) << " MiB";
    else out << "\nProcess RSS: unavailable";
#else
    out << "\nProcess RSS: unavailable on this platform";
#endif
    if (frame.ms > 33.333) out << "\nALERT: frame over 33.33 ms";
    if (frame.ms > stats.median*2 && frame.ms > 16.667) out << " | Stutter (>2x median)";
    if(frame.dropped) out << "\nDropped scope events: " << frame.dropped;
    if(!m_baseline.empty()) out << "\nMean delta from baseline: " << stats.mean-Bubble::summarize_frames(m_baseline).mean << " ms";
    out << "\nVRAM / asset bytes / allocation tracking: unavailable";
    out << "\n" << m_status;
    double cpu = 0;
    for (const auto& e : frame.events) if (!e.parent) cpu += e.total_us;
    out << "\nCPU instrumented root scopes (sum): " << cpu/1000 << " ms";
    m_summary->set_buffer(out.str());
    std::ostringstream details; details << std::fixed << std::setprecision(3);
    details << "CPU timeline: thread | name | offset | total / self ms\n";
    // Completion order is reversed for nested scopes; timestamps restore call order.
    auto events = frame.events;
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) { return a.start_us < b.start_us; });
    std::size_t shown = 0;
    for(const auto& e : events) {
        const auto thread = std::to_string(e.thread);
        if(!m_filter.empty() && e.name.find(m_filter)==std::string::npos && e.category.find(m_filter)==std::string::npos && thread.find(m_filter)==std::string::npos) continue;
        if(++shown > 100) { details << "More events available in export\n"; break; }
        std::size_t depth = 0;
        auto parent = e.parent;
        while (parent && depth < events.size()) {
            const auto found = std::find_if(events.begin(), events.end(), [parent](const auto& v) { return v.id == parent; });
            if (found == events.end()) break;
            ++depth; parent = found->parent;
        }
        details << thread << " | " << std::string(depth*2, ' ') << e.name << " | +" << (e.start_us-events.front().start_us)/1000 << " | " << e.total_us/1000 << " / " << e.self_us/1000 << "\n";
    }
    struct cost { std::size_t calls{}; double self{}; };
    std::map<std::string, cost> totals;
    for (const auto& e : events) {
        const auto thread = std::to_string(e.thread);
        if (!m_filter.empty() && e.name.find(m_filter) == std::string::npos && e.category.find(m_filter) == std::string::npos && thread.find(m_filter) == std::string::npos) continue;
        auto& cost = totals[e.name]; ++cost.calls; cost.self += e.self_us;
    }
    std::vector<std::pair<std::string, cost>> ranked(totals.begin(), totals.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) { return a.second.self > b.second.self; });
    details << "\nTop scopes: name | calls | self ms\n";
    for (std::size_t i=0; i<std::min<std::size_t>(10, ranked.size()); ++i)
        details << ranked[i].first << " | " << ranked[i].second.calls << " | " << ranked[i].second.self/1000 << "\n";
    m_details->set_buffer(details.str());
}
}
