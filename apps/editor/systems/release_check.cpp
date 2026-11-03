#include "release_check.hpp"
#include <curl/curl.h>
#include <memory>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace editor::ui {
namespace {
std::size_t receive(char* data, std::size_t size, std::size_t count, void* target) noexcept {
    auto& body = *static_cast<std::string*>(target);
    if (size != 1 || count > 1024 * 1024 - body.size()) return 0;
    try { body.append(data, count); } catch (...) { return 0; }
    return count;
}
}
std::optional<int> launch_managed_editor(int argc, char** argv) {
    if (std::getenv("BENGINE_MANAGED_UPDATE")) return std::nullopt;

    std::error_code error;
#ifdef _WIN32
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (!length || length == executable.size()) return std::nullopt;
    executable.resize(length);
    auto root = std::filesystem::path(executable).parent_path();
    constexpr auto updater_name = L"engine_updater.exe";
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error) return std::nullopt;
    auto root = executable.parent_path();
    constexpr auto updater_name = "engine_updater";
#endif
    if (root.parent_path().filename() == ".engine-updates")
        root = root.parent_path().parent_path();
    const auto updater = root / updater_name;
    if (!std::filesystem::is_regular_file(root / "release.json", error) ||
        !std::filesystem::is_regular_file(updater, error))
        return std::nullopt;

#ifdef _WIN32
    // Preserve the original Unicode arguments and their Windows quoting.
    static_cast<void>(argc);
    static_cast<void>(argv);
    const std::wstring original = GetCommandLineW();
    const auto end = original.starts_with(L'"')
        ? original.find(L'"', 1) : original.find_first_of(L" \t");
    const auto arguments = end == std::wstring::npos ? L"" :
        original.substr(end + (original.starts_with(L'"') ? 1 : 0));
    auto command = L'"' + updater.native() + L'"' + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(updater.c_str(), command.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &startup, &process))
        return std::nullopt;
    CloseHandle(process.hThread);
    DWORD result = 1;
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    return static_cast<int>(result);
#else
    auto program = updater.string();
    std::vector<char*> arguments{program.data()};
    arguments.insert(arguments.end(), argv + 1, argv + argc);
    arguments.push_back(nullptr);
    execv(program.c_str(), arguments.data());
    return std::nullopt;
#endif
}

std::string check_latest_release() {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) return "Update check unavailable";
    struct cleanup { ~cleanup() { curl_global_cleanup(); } } guard;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle) return "Update check unavailable";
    std::string body;
    curl_easy_setopt(handle.get(), CURLOPT_URL, "https://api.github.com/repos/D4nielStone/cpp-bengine/releases/latest");
    curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, "BubbleEngine/" BUBBLE_ENGINE_VERSION);
    curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &body);
    const auto result = curl_easy_perform(handle.get());
    long status = 0;
    curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
    if (result != CURLE_OK || status != 200) return "Update check unavailable (offline or no release)";
    return release_status(body);
}
void open_engine_releases() {
    constexpr const char* url = "https://github.com/D4nielStone/cpp-bengine/releases/latest";
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    // Double fork lets the desktop opener outlive the editor without a zombie.
    const auto child = fork();
    if (child == 0) {
        if (fork() == 0) {
            execlp("xdg-open", "xdg-open", url, static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }
    if (child > 0) while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
#endif
}
}
