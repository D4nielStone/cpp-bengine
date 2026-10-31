#include "release_check.hpp"
#include <curl/curl.h>
#include <memory>
#include <cerrno>
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
