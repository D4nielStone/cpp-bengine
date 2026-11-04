#include "systems/code_system.hpp"
#include "debugging/script_log.hpp"

#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace commons {

code_system::~code_system() = default;

code_system::script_state& code_system::initialize(
    const std::shared_ptr<code>& script, std::uint32_t id
) {
    auto& state = m_scripts[script.get()];
    if (!state.instance.expired())
        return state;

    state = {script, false};
    try {
        script->setup();
    } catch (const sol::error& error) {
        state.failed = true;
        script_log::instance().write(id, std::string("Initialization failed: ") + error.what());
    }
    return state;
}

void code_system::setup(const std::shared_ptr<ecs>& registry) {
    m_scripts.clear();
    m_last_update = std::chrono::steady_clock::now();
    if (!registry)
        return;

    registry->cada<code>([this, &registry](std::uint32_t id) {
        const auto component = registry->get<code>(id);
        if (!component)
            return;
        auto entries = component->scripts;
        entries.insert(entries.begin(), component);
        for (const auto& script : entries) {
            if (!script)
                continue;
            script->reg = registry;
            script->my_object = id;
            initialize(script, id);
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
            const auto component = registry->get<code>(id);
            if (!component) return;
            auto entries = component->scripts;
            entries.insert(entries.begin(), component);
            for (const auto& script : entries) {
                if (!script)
                    continue;
                script->reg = registry;
                script->my_object = id;
                present.insert(script.get());
                auto& state = initialize(script, id);
                if (state.failed)
                    continue;

                try {
                    auto bubble = script->estado_lua["bubble"].get<sol::table>();
                    bubble["delta_time"] = delta_time;
                    auto time = script->estado_lua["Time"].get<sol::table>();
                    time["deltaTime"] = delta_time;
                    time["time"] = time["time"].get<float>() + delta_time;
                    script->update();
                } catch (const sol::error& error) {
                    state.failed = true;
                    script_log::instance().write(id, std::string("Update failed: ") + error.what());
                }
            }
        }
    );

    std::erase_if(m_scripts, [&](const auto& entry) { return !present.contains(entry.first); });
}

}
