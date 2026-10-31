#pragma once
#include "release_check.hpp"
#include <lay/linear.hpp>
#include <elem/button.hpp>
#include <elem/text.hpp>
#include <future>
#include <chrono>

namespace editor::ui {
class release_indicator final : public bgui::linear {
public:
    explicit release_indicator(std::shared_future<std::string> pending) : bgui::linear(bgui::orientation::vertical),
        result(std::move(pending)) {
        status = &add<bgui::text>("Engine " BUBBLE_VERSION_COMPLETA_STR " / Checking releases...", 0.28f);
        add<bgui::button>("Releases / Download update", 0.28f, [] { open_engine_releases(); });
    }
    void on_update() override {
        bgui::linear::on_update();
        if (!consumed && result.valid() && result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            consumed = true;
            try { status->set_buffer("Engine " BUBBLE_VERSION_COMPLETA_STR " / " + result.get()); }
            catch (...) { status->set_buffer("Engine " BUBBLE_VERSION_COMPLETA_STR " / Update check unavailable"); }
        }
    }
private:
    bgui::text* status{};
    bool consumed{false};
    std::shared_future<std::string> result;
};
}
