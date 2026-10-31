#include <gtest/gtest.h>
#include "build_process.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>

namespace {
class BuildProcessTest : public testing::Test {
protected:
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("bengine build process " + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override { std::filesystem::create_directories(root / "scripts"); }
    void TearDown() override { std::filesystem::remove_all(root); }
};
}

TEST_F(BuildProcessTest, MissingHostScriptIsReportedWithoutStarting) {
    editor::ui::build_process process;
    std::string error;
    EXPECT_FALSE(process.start(root, root / "output", editor::ui::build_target::linux, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(process.running(error));
}

TEST_F(BuildProcessTest, HostScriptReceivesPathsAndReportsProgressAndExitCode) {
#ifdef _WIN32
    std::ofstream(root / "scripts/compile_runtime.ps1") <<
        "param($Target, $RepositoryRoot, $BuildDirectory, $ProgressFile)\n"
        "Write-Output $RepositoryRoot\n"
        "[System.IO.File]::WriteAllText($ProgressFile, \"100|done`n\")\n"
        "exit 7\n";
#else
    std::ofstream(root / "scripts/compile_runtime.sh") <<
        "printf '%s\\n' \"$2\"\n"
        "printf '100|done\\n' > \"$4\"\n"
        "exit 7\n";
#endif
    editor::ui::build_process process;
    std::string error;
    ASSERT_TRUE(process.start(root, root / "output with spaces",
        editor::ui::build_target::windows, error)) << error;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (process.running(error) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_FALSE(process.running(error)) << error;
    EXPECT_EQ(process.exit_code(), 7);
    std::string status;
    EXPECT_FLOAT_EQ(process.progress(status), 100.f);
    EXPECT_EQ(status, "done");
    std::ifstream log(process.log_path());
    const std::string contents{std::istreambuf_iterator<char>(log), {}};
    EXPECT_NE(contents.find(root.string()), std::string::npos);
}

TEST_F(BuildProcessTest, GameExportKeepsLogsAndProgressOutsideOutputAndPassesProject) {
#ifdef _WIN32
    std::ofstream(root / "scripts/platform_build.ps1") <<
        "param($Target, $RepositoryRoot, $BuildDirectory, $ProgressFile, $ProjectFile, $Workspace)\n"
        "Write-Output $ProjectFile\n"
        "[System.IO.File]::WriteAllText($ProgressFile, \"100|exported`n\")\n"
        "exit 0\n";
#else
    std::ofstream(root / "scripts/platform_build.sh") <<
        "printf '%s\\n' \"$5\"\n"
        "printf '100|exported\\n' > \"$4\"\n";
#endif
    const auto output = root / "clean output";
    const auto project = root / "game/project.bengine";
    editor::ui::build_process process;
    std::string error;
    ASSERT_TRUE(process.start(root, output, editor::ui::build_target::linux, project, error)) << error;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (process.running(error) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_FALSE(process.running(error)) << error;
    EXPECT_EQ(process.exit_code(), 0);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_NE(process.log_path().parent_path(), output);
    std::ifstream log(process.log_path());
    const std::string contents{std::istreambuf_iterator<char>(log), {}};
    EXPECT_NE(contents.find(project.string()), std::string::npos);
    std::filesystem::remove_all(process.log_path().parent_path());
}

#include "../apps/editor/core/distribution_paths.hpp"

TEST(DistributionPaths, InstalledPathsFollowTheManagerAndDevelopmentPathsStayIntact) {
    const char* previous = std::getenv("BENGINE_HOME");
    const std::string saved = previous ? previous : "";
    const bool had_previous = previous != nullptr;
    const auto set_home = [](const char* value) {
#ifdef _WIN32
        _putenv_s("BENGINE_HOME", value ? value : "");
#else
        if (value) setenv("BENGINE_HOME", value, 1);
        else unsetenv("BENGINE_HOME");
#endif
    };
    set_home(nullptr);
    EXPECT_EQ(editor::distribution::path("source", "checkout"), std::filesystem::path("checkout"));
    set_home("/installed/Bubble Engine/v1.0.0");
    EXPECT_EQ(editor::distribution::path("source", "checkout"),
        std::filesystem::path("/installed/Bubble Engine/v1.0.0/source"));
    EXPECT_EQ(editor::distribution::path("game_packager", "old-build/game_packager"),
        std::filesystem::path("/installed/Bubble Engine/v1.0.0/game_packager"));
    set_home(had_previous ? saved.c_str() : nullptr);
}
