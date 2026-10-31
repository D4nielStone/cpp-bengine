"""GPU-free contracts for the platform build entry points."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class PlatformBuildTests(unittest.TestCase):
    def test_linux_native_uses_host_compiler_and_stages_assets(self):
        with tempfile.TemporaryDirectory(prefix="bengine build ") as directory:
            root = Path(directory)
            commands = root / "commands"
            commands.mkdir()
            output = root / "output"
            log = root / "cmake.log"
            for name, body in {
                "cmake": 'printf "%s\\n" "$*" >> "$BUILD_TEST_LOG"\n',
                "pkg-config": "exit 0\n",
                "curl": "exit 91\n",
                "git": "exit 92\n",
            }.items():
                script = commands / name
                script.write_text("#!/bin/sh\n" + body)
                script.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       BUILD_TEST_LOG=str(log), VCPKG_ROOT=str(root / "missing-vcpkg"))
            result = subprocess.run(["sh", str(REPO / "scripts/platform_build.sh"),
                                     "linux", str(REPO), str(output), str(output / "progress")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            arguments = log.read_text()
            self.assertIn("-DBUILD_EDITOR=OFF", arguments)
            self.assertNotIn("CMAKE_TOOLCHAIN_FILE", arguments)
            self.assertIn("--target runtime_app", arguments)
            self.assertTrue((output / "platform-builds/linux/apps/assets/shaders").is_dir())
            self.assertTrue((output / "progress").read_text().startswith("100|"))

    def test_cmake_runtime_only_does_not_require_editor_sources_or_png(self):
        with tempfile.TemporaryDirectory(prefix="bengine cmake ") as directory:
            root = Path(directory)
            apps = root / "apps"
            (apps / "editor/systems").mkdir(parents=True)
            shutil.copyfile(REPO / "apps/CMakeLists.txt", apps / "CMakeLists.txt")
            (apps / "runtime_app.cpp").write_text("int main() {}")
            (apps / "editor/systems/project_persistence_system.cpp").write_text("")
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(BuildContract LANGUAGES CXX)\n"
                "add_library(commons INTERFACE)\nadd_subdirectory(apps)\n")
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(root / "out"),
                                     "-DBUILD_RUNTIME=ON", "-DBUILD_EDITOR=OFF",
                                     "-DCMAKE_DISABLE_FIND_PACKAGE_PNG=ON"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(["cmake", "--build", str(root / "out"),
                                     "--target", "runtime_app"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            # With both applications disabled, even runtime sources are optional.
            (apps / "runtime_app.cpp").unlink()
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(root / "disabled"),
                                     "-DBUILD_RUNTIME=OFF", "-DBUILD_EDITOR=OFF"],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def powershell(self):
        pwsh = shutil.which("pwsh")
        if not pwsh:
            cached = Path.home() / ".cache/cpp-bengine/toolchains/powershell-7.6.8/pwsh"
            pwsh = str(cached) if cached.is_file() else None
        if not pwsh:
            self.skipTest("PowerShell is not installed")
        return pwsh

    def test_windows_native_does_not_require_wsl_and_stages_dlls(self):
        pwsh = self.powershell()
        with tempfile.TemporaryDirectory(prefix="bengine windows ") as directory:
            root = Path(directory)
            commands = root / "commands"
            commands.mkdir()
            output = root / "output"
            output.mkdir()
            release = output / "platform-builds/windows/apps/Release"
            release.mkdir(parents=True)
            (release / "runtime_app.exe").write_text("test executable")
            (release / "dependency.dll").write_text("test dll")
            log = root / "cmake.log"
            for name, body in {
                "cmake": 'printf "%s\\n" "$*" >> "$BUILD_TEST_LOG"\n',
                "vcpkg.exe": "exit 0\n",
                "wsl.exe": "exit 93\n",
            }.items():
                script = commands / name
                script.write_text("#!/bin/sh\n" + body)
                script.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       VCPKG_ROOT=str(commands), BUILD_TEST_LOG=str(log))
            args = [pwsh, "-NoLogo", "-NoProfile", "-File",
                    str(REPO / "scripts/platform_build.ps1"), "-Target", "windows",
                    "-RepositoryRoot", str(REPO), "-BuildDirectory", str(output),
                    "-ProgressFile", str(output / "progress")]
            result = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Visual Studio 17 2022", log.read_text())
            self.assertTrue((release.parent / "dependency.dll").is_file())
            self.assertTrue((release.parent / "assets/shaders").is_dir())
            self.assertTrue((output / "progress").read_text().startswith("100|"))
            # A failed configure must propagate and must not report success.
            (commands / "cmake").write_text("#!/bin/sh\nexit 17\n")
            result = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((output / "progress").read_text().startswith("100|"))

    def test_windows_linux_target_uses_wsl_and_propagates_exit_code(self):
        pwsh = self.powershell()
        with tempfile.TemporaryDirectory(prefix="bengine wsl ") as directory:
            root = Path(directory)
            wsl = root / "wsl.exe"
            wsl.write_text(
                '#!/bin/sh\n'
                'if [ "$2" = uname ]; then echo 6.6-microsoft-standard-WSL2; exit 0; fi\n'
                'if [ "$2" = wslpath ]; then printf "%s\\n" "$4"; exit 0; fi\n'
                'printf "%s\\n" "$@" > "$BUILD_TEST_LOG"\nexit 23\n')
            wsl.chmod(0o755)
            log = root / "wsl.log"
            env = dict(os.environ, PATH=str(root) + os.pathsep + os.environ["PATH"],
                       BUILD_TEST_LOG=str(log))
            result = subprocess.run([pwsh, "-NoLogo", "-NoProfile", "-File",
                                     str(REPO / "scripts/platform_build.ps1"),
                                     "-Target", "linux", "-RepositoryRoot", str(REPO),
                                     "-BuildDirectory", str(root / "output with spaces"),
                                     "-ProgressFile", str(root / "progress")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            args = log.read_text().splitlines()
            self.assertEqual(args[:3], ["--exec", "bash", str(REPO / "scripts/platform_build.sh")])
            self.assertEqual(args[3:], ["linux", str(REPO), str(root / "output with spaces"),
                                       str(root / "progress")])


if __name__ == "__main__":
    unittest.main()
