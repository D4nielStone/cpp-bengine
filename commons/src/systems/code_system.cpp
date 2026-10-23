#include "systems/code_system.hpp"

#include <iostream>
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace commons {

code_system::~code_system() = default;

void code_system::setup(const std::shared_ptr<ecs>& registry) {
    m_initialized.clear();
    m_failed.clear();
    m_last_update = std::chrono::steady_clock::now();
    if (!registry)
        return;

    registry->cada<code>([this, &registry](const std::uint32_t id) {
        const auto script = registry->get<code>(id);
        if (!script)
            return;

        m_initialized.insert(script.get());
        try {
            script->setup();
        } catch (const sol::error& error) {
            m_failed.insert(script.get());
            std::cerr << "[lua] Could not initialize script for entity "
                      << id << ": " << error.what() << '\n';
        }
    });
}

void code_system::update(const std::shared_ptr<ecs>& registry) {
    if (!registry)
        return;

    const auto now = std::chrono::steady_clock::now();
    const float delta_time = m_last_update.time_since_epoch().count() == 0
        ? 0.f
        : std::clamp(
              std::chrono::duration<float>(now - m_last_update).count(),
              0.f,
              0.1f
          );
    m_last_update = now;

    std::unordered_set<const code*> present;
    registry->cada<code>(
        [this, &registry, &present, delta_time](const std::uint32_t id) {
            const auto script = registry->get<code>(id);
            if (!script)
                return;

            present.insert(script.get());
            if (!m_initialized.contains(script.get())) {
                m_initialized.insert(script.get());
                try {
                    script->setup();
                } catch (const sol::error& error) {
                    m_failed.insert(script.get());
                    std::cerr << "[lua] Could not initialize script for entity "
                              << id << ": " << error.what() << '\n';
                    return;
                }
            }

            if (m_failed.contains(script.get()))
                return;
            try {
                auto bubble = script->estado_lua["bubble"].get<sol::table>();
                bubble["delta_time"] = delta_time;
                script->update();
            } catch (const sol::error& error) {
                m_failed.insert(script.get());
                std::cerr << "[lua] Script update failed for entity "
                          << id << ": " << error.what() << '\n';
            }
        }
    );

    for (auto script = m_initialized.begin(); script != m_initialized.end();) {
        if (!present.contains(*script)) {
            m_failed.erase(*script);
            script = m_initialized.erase(script);
        } else {
            ++script;
        }
    }
}

}
