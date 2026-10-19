#pragma once

#include <string>

namespace editor::ui {

class ui_persistence_system final {
public:
    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error) const;
};

}
