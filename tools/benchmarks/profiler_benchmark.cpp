#include "profiling/profiler.hpp"
#include <iostream>
#include <thread>
int main() {
    constexpr int frames = 120, scopes = 1000;
    auto& profiler = Bubble::profiler::instance();
    for (const bool enabled : {false, true}) {
        profiler.enabled = enabled && BENGINE_PROFILING;
        const auto start = std::chrono::steady_clock::now();
        for(int frame=0;frame<frames;++frame) {
            profiler.next_frame(1./60);
            for(int i=0;i<scopes;++i) { BUBBLE_PROFILE("benchmark", "CPU"); }
        }
        const auto ns = std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();
        std::cout << (enabled ? "enabled" : "disabled") << ": " << ns/(frames*scopes) << " ns/scope (includes frame collection)\n";
    }
}
