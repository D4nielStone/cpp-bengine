#include <gtest/gtest.h>
#include "native_file_dialog.hpp"
#ifndef _WIN32
#include <cstdlib>
#include <fstream>
#include <unistd.h>

class NativeDialogTest : public testing::Test {
protected:
    std::filesystem::path root;
    std::string old_path;
    std::string old_desktop;
    bool had_path{}, had_desktop{};
    void SetUp() override {
        char pattern[] = "/tmp/bengine-dialog-XXXXXX";
        root = mkdtemp(pattern);
        if (const char* value = getenv("PATH")) { had_path = true; old_path = value; }
        if (const char* value = getenv("XDG_CURRENT_DESKTOP")) { had_desktop = true; old_desktop = value; }
        setenv("PATH", root.c_str(), 1);
        setenv("XDG_CURRENT_DESKTOP", "GNOME", 1);
    }
    void TearDown() override {
        if (had_path) setenv("PATH", old_path.c_str(), 1); else unsetenv("PATH");
        if (had_desktop) setenv("XDG_CURRENT_DESKTOP", old_desktop.c_str(), 1); else unsetenv("XDG_CURRENT_DESKTOP");
        std::filesystem::remove_all(root);
    }
    void program(const char* name, const std::string& body) {
        const auto path = root / name;
        std::ofstream(path) << "#!/bin/sh\n" << body;
        std::filesystem::permissions(path, std::filesystem::perms::owner_all);
    }
};

TEST_F(NativeDialogTest, PreservesPathsAndPassesFolderModeWithoutShellExpansion) {
    program("zenity", "printf '%s\\n' \"$@\" > '" + (root / "arguments").string() + "'\nprintf '%s\\n' '/tmp/pasta com espaço/projeto.bengine'\n");
    std::string error;
    const auto selected = editor::browse_native("/tmp/$(touch nope); 'folder'", true, error);
    EXPECT_EQ(selected, "/tmp/pasta com espaço/projeto.bengine");
    EXPECT_TRUE(error.empty());
    std::ifstream input(root / "arguments");
    const std::string arguments((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(arguments.find("--directory\n"), std::string::npos);
    EXPECT_NE(arguments.find("--filename=/tmp/$(touch nope); 'folder'\n"), std::string::npos);
}
TEST_F(NativeDialogTest, ProjectDialogFiltersSupportedExtensions) {
    program("zenity", "printf '%s\\n' \"$@\" > '" + (root / "arguments").string() + "'\nexit 1\n");
    std::string error;
    editor::browse_native({}, false, error);
    std::ifstream input(root / "arguments");
    const std::string arguments((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(arguments.find("*.bengine *.scene *.world"), std::string::npos);
    EXPECT_EQ(arguments.find("--directory"), std::string::npos);
}
TEST_F(NativeDialogTest, CancellationKeepsSelectionEmpty) {
    program("zenity", "exit 1\n");
    std::string error = "old error";
    EXPECT_TRUE(editor::browse_native({}, false, error).empty());
    EXPECT_TRUE(error.empty());
}
TEST_F(NativeDialogTest, FallsBackToKdialogWhenZenityIsMissing) {
    program("kdialog", "printf '%s\\n' '/tmp/project.scene'\n");
    std::string error;
    EXPECT_EQ(editor::browse_native({}, false, error), "/tmp/project.scene");
    EXPECT_TRUE(error.empty());
}
TEST_F(NativeDialogTest, ReportsMissingProgramsAndBackendFailure) {
    std::string error;
    EXPECT_TRUE(editor::browse_native({}, false, error).empty());
    EXPECT_FALSE(error.empty());
    program("zenity", "exit 2\n");
    EXPECT_TRUE(editor::browse_native({}, false, error).empty());
    EXPECT_FALSE(error.empty());
}
#endif
