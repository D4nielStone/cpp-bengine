#pragma once
#include "release_check.hpp"
#include "editor/core/distribution_paths.hpp"
#include <lay/linear.hpp>
#include <elem/button.hpp>
#include <elem/text.hpp>
#include <future>
#include <chrono>
#include <cstdlib>
#include <fstream>

namespace editor::ui {
class release_indicator final : public bgui::linear {
public:
    explicit release_indicator(std::shared_future<std::string> pending) : bgui::linear(bgui::orientation::horizontal),
        result(std::move(pending)) {
        style.layout.require_mode(bgui::mode::match_parent, bgui::mode::wrap_content);
        style.layout.set_padding(8, 4);
        status = &add<bgui::text>("Engine " BUBBLE_VERSION_COMPLETA_STR " / Checking releases...", 0.28f);
        status->style.layout.require_mode(bgui::mode::stretch, bgui::mode::wrap_content);
        if (!managed)
            add<bgui::button>("Releases / Download update", 0.28f, [] { open_engine_releases(); });
    }
    void on_update() override {
        bgui::linear::on_update();
        if (managed) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_poll) {
                next_poll = now + std::chrono::seconds(1);
                std::ifstream input(editor::distribution::user_path(".engine-updates/status"));
                std::string message;
                if (!std::getline(input, message))
                    message = "Update status unavailable";
                if (message != last_message) {
                    last_message = std::move(message);
                    status->set_buffer("Engine " BUBBLE_VERSION_COMPLETA_STR " / " + last_message);
                }
            }
            return;
        }
        if (!consumed && result.valid() && result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            consumed = true;
            try { status->set_buffer("Engine " BUBBLE_VERSION_COMPLETA_STR " / " + result.get()); }
            catch (...) { status->set_buffer("Engine " BUBBLE_VERSION_COMPLETA_STR " / Update check unavailable"); }
        }
    }
private:
    bgui::text* status{};
    bool consumed{false};
    const bool managed = std::getenv("BENGINE_MANAGED_UPDATE") != nullptr;
    std::chrono::steady_clock::time_point next_poll{};
    std::string last_message;
    std::shared_future<std::string> result;
};
}
