#include <gtest/gtest.h>
#include "runtime_process.hpp"
#include "debugging/script_log.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace {
namespace fs = std::filesystem;
class RuntimeProcessTest : public testing::Test {
protected:
    fs::path root = fs::temp_directory_path() / ("bengine runtime with spaces " +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::path project = root / "snapshot.bengine";
    fs::path scene = root / "snapshot.scene";
    void SetUp() override {
        fs::create_directories(root);
        commons::script_log::instance().clear();
        snapshot();
    }
    void TearDown() override { fs::remove_all(root); }
    void snapshot(const char* mode = "wait") {
        std::ofstream(project) << mode;
        std::ofstream(scene) << "temporary scene";
    }
    template<class Predicate> bool wait_for(Predicate predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        do {
            if (predicate()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
    void expect_clean() {
        EXPECT_FALSE(fs::exists(project));
        EXPECT_FALSE(fs::exists(scene));
        EXPECT_FALSE(fs::exists(project.string() + ".lua-log"));
    }
};
}

TEST_F(RuntimeProcessTest, MissingExecutableReportsErrorAndCleansSnapshot) {
    editor::ui::runtime_process process;
    std::string error;
    EXPECT_FALSE(process.start(root / "missing runtime", project, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(process.running(error));
    expect_clean();
}

TEST_F(RuntimeProcessTest, PlayPauseResumeStopAndPlayAgain) {
    editor::ui::runtime_process process;
    std::string error;
    for (int run = 0; run < 2; ++run) {
        snapshot();
        ASSERT_TRUE(process.start(RUNTIME_PROCESS_HELPER, project, error)) << error;
        ASSERT_TRUE(wait_for([&] {
            process.collect_script_logs();
            return !commons::script_log::instance().lines().empty();
        }));
        EXPECT_FALSE(process.start(RUNTIME_PROCESS_HELPER, project, error));
        ASSERT_TRUE(process.running(error)) << error;
        ASSERT_TRUE(process.set_paused(true, error)) << error;
        EXPECT_TRUE(process.running(error));
        ASSERT_TRUE(process.set_paused(false, error)) << error;
        ASSERT_TRUE(process.set_paused(true, error)) << error;
        ASSERT_TRUE(process.stop(error)) << error;
        EXPECT_FALSE(process.running(error));
        EXPECT_TRUE(process.stop(error));
        expect_clean();
        commons::script_log::instance().clear();
    }
}

TEST_F(RuntimeProcessTest, NaturalExitCollectsLogsAndCleansSnapshot) {
    snapshot("exit");
    editor::ui::runtime_process process;
    std::string error;
    ASSERT_TRUE(process.start(RUNTIME_PROCESS_HELPER, project, error)) << error;
    ASSERT_TRUE(wait_for([&] { return !process.running(error); }));
    EXPECT_TRUE(error.empty()) << error;
    ASSERT_FALSE(commons::script_log::instance().lines().empty());
    EXPECT_EQ(commons::script_log::instance().lines().back(), "runtime helper ready");
    expect_clean();
}

TEST_F(RuntimeProcessTest, DestructionStopsChildAndCleansSnapshot) {
    {
        editor::ui::runtime_process process;
        std::string error;
        ASSERT_TRUE(process.start(RUNTIME_PROCESS_HELPER, project, error)) << error;
    }
    expect_clean();
}

#ifdef BENGINE_RUNTIME_EXECUTABLE
TEST_F(RuntimeProcessTest, RealDemoStartsAndRestartsThroughEditorProcess) {
    if (!std::getenv("BENGINE_RUNTIME_SMOKE"))
        GTEST_SKIP() << "Set BENGINE_RUNTIME_SMOKE=1 with an OpenGL display to run the real demo";
    fs::copy(fs::path(BENGINE_SOURCE_DIR) / "projects/fpp_demo", root / "demo",
        fs::copy_options::recursive);
    const auto demo = root / "demo/project.bengine";
    const auto original = fs::file_size(fs::path(BENGINE_SOURCE_DIR) / "projects/fpp_demo/project.bengine");
    editor::ui::runtime_process process;
    std::string error;
    for (int run = 0; run < 2; ++run) {
        if (run) fs::copy_file(fs::path(BENGINE_SOURCE_DIR) / "projects/fpp_demo/project.bengine", demo);
        commons::script_log::instance().clear();
        ASSERT_TRUE(process.start(BENGINE_RUNTIME_EXECUTABLE, demo, error)) << error;
        ASSERT_TRUE(wait_for([&] {
            if (!process.running(error)) return true;
            for (const auto& line : commons::script_log::instance().lines())
                if (line.find("FPP player body ready.") != std::string::npos) return true;
            return false;
        }));
        ASSERT_TRUE(process.running(error)) << "Runtime exited before loading the demo: " << error;
        bool ready = false;
        for (const auto& line : commons::script_log::instance().lines()) {
            ready |= line.find("FPP player body ready.") != std::string::npos;
            EXPECT_EQ(line.find("[error]"), std::string::npos) << line;
        }
        ASSERT_TRUE(ready);
        ASSERT_TRUE(process.set_paused(true, error)) << error;
        ASSERT_TRUE(process.set_paused(false, error)) << error;
        ASSERT_TRUE(process.stop(error)) << error;
    }
    EXPECT_EQ(fs::file_size(fs::path(BENGINE_SOURCE_DIR) / "projects/fpp_demo/project.bengine"), original);
}
#endif
