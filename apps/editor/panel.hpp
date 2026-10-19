#pragma once

#include <bgui.hpp>
#include <lay/dock.hpp>

namespace editor::ui {

class panel {
public:
    virtual ~panel() = default;

    virtual void build(bgui::dock& parent) = 0;
    virtual void update() = 0;
};

}