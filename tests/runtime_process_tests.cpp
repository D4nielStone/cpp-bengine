#include <gtest/gtest.h>
#include "runtime_process.hpp"
#include "runtime_scene_preview.hpp"
#include "project_persistence_system.hpp"
#include "project_creation_system.hpp"
#include "components/camera.hpp"
#include "os/window.hpp"
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
TEST_F(RuntimeProcessTest, FppTemplateStartsAndRestartsThroughEditorProcess) {
    if (!std::getenv("BENGINE_RUNTIME_SMOKE"))
        GTEST_SKIP() << "Set BENGINE_RUNTIME_SMOKE=1 with an OpenGL display to run the FPP template";
    const auto template_directory = root / "fpp_template";
    std::string error;
    ASSERT_TRUE(editor::ui::create_project_files(template_directory, error,
        editor::ui::project_template::fpp)) << error;
    const auto template_project = template_directory / "project.bengine";
    const auto original = fs::file_size(template_project);
    commons::window window("Editor runtime snapshot test", 100, 100);
    auto registry = window.get_ecs();
    auto editor_camera = registry->create();
    registry->add<commons::camera>(editor_camera);
    editor::ui::project_persistence_system persistence(registry, editor_camera.id);
    persistence.set_migration_writes_enabled(false);
    editor::ui::runtime_process process;
    ASSERT_TRUE(persistence.load(template_project.string(), error)) << error;
    for (int run = 0; run < 2; ++run) {
        ASSERT_TRUE(persistence.save(project.string(), error)) << error;
        commons::script_log::instance().clear();
        ASSERT_TRUE(process.start(BENGINE_RUNTIME_EXECUTABLE, project, error)) << error;
        ASSERT_TRUE(wait_for([&] {
            if (!process.running(error)) return true;
            for (const auto& line : commons::script_log::instance().lines())
                if (line.find("FPP player body ready.") != std::string::npos) return true;
            return false;
        }));
        ASSERT_TRUE(process.running(error)) << "Runtime exited before loading the FPP template: " << error;
        bool ready = false;
        for (const auto& line : commons::script_log::instance().lines()) {
            ready |= line.find("FPP player body ready.") != std::string::npos;
            EXPECT_EQ(line.find("Initialization failed:"), std::string::npos) << line;
            EXPECT_EQ(line.find("Update failed:"), std::string::npos) << line;
        }
        ASSERT_TRUE(ready);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (std::chrono::steady_clock::now() < deadline) {
            ASSERT_TRUE(process.running(error)) << error;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (const auto& line : commons::script_log::instance().lines()) {
            EXPECT_EQ(line.find("Initialization failed:"), std::string::npos) << line;
            EXPECT_EQ(line.find("Update failed:"), std::string::npos) << line;
        }
        const auto preview_path = fs::path(project.string() + ".preview");
        ASSERT_TRUE(fs::exists(preview_path));
        const auto preview = editor::ui::project_files::read(preview_path);
        ASSERT_TRUE(editor::ui::apply_runtime_preview(preview, registry, editor_camera.id));
        ASSERT_TRUE(process.set_paused(true, error)) << error;
        ASSERT_TRUE(process.set_paused(false, error)) << error;
        ASSERT_TRUE(process.stop(error)) << error;
        EXPECT_FALSE(fs::exists(preview_path));
        expect_clean();
    }
    EXPECT_TRUE(fs::exists(template_project));
    EXPECT_TRUE(fs::exists(template_directory / "project.scene"));
    EXPECT_EQ(fs::file_size(template_project), original);
}
#endif
