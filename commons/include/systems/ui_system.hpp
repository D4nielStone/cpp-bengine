#pragma once

#include <memory>

#include "systems/system.hpp"

namespace COMMONS_NS {
    class ui_system final : public system {
    public:
        ~ui_system() override;

        void setup(const std::shared_ptr<ecs>&) override;
        void update(const std::shared_ptr<ecs>&) override;

    private:
        bool m_initialized{false};
    };
}
