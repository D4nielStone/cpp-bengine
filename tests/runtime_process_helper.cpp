#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 5 || std::string(argv[1]) != "--project" ||
        std::string(argv[3]) != "--script-log") return 2;
    std::ifstream project(argv[2]);
    std::string mode;
    std::getline(project, mode);
    std::ofstream log(argv[4], std::ios::binary);
    const std::string message = "runtime helper ready";
    const auto size = static_cast<std::uint32_t>(message.size());
    log.write(reinterpret_cast<const char*>(&size), sizeof(size));
    log.write(message.data(), size);
    log.flush();
    if (mode == "exit") return 0;
    for (;;) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
