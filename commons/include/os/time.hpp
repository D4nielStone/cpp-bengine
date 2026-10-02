#pragma once

#include <chrono>

#include "commons_namespace.hpp"

namespace COMMONS_NS {
    struct time {
        time() : elapsed_time(std::chrono::high_resolution_clock::now()) {}

        double get_delta_time() {
            return delta.count();
        }

        void calculateDT() {
            const auto now = std::chrono::high_resolution_clock::now();
            delta = now - elapsed_time;
            elapsed_time = now;
        }

        std::chrono::duration<double> delta{};
        std::chrono::time_point<std::chrono::high_resolution_clock> elapsed_time;
    };
}
