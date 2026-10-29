#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
struct GLFWwindow;
namespace commons::detail {
using file_drop_handler = std::function<void(std::vector<std::filesystem::path>, int, int)>;
class file_drop_bridge {
public:
    virtual ~file_drop_bridge() = default;
    virtual bool hovered() const { return false; }
    virtual void poll_events() {}
};
std::unique_ptr<file_drop_bridge> make_file_drop_bridge(GLFWwindow*, file_drop_handler);
}
