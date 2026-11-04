"""GPU-free contracts for the platform build entry points."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]


class PlatformBuildTests(unittest.TestCase):
    @unittest.skipUnless(os.name == "posix", "POSIX build helper")
    def test_build_helper_configures_release_for_single_config_generators(self):
        with tempfile.TemporaryDirectory(prefix="bengine helper ") as directory:
            root = Path(directory)
            commands = root / "commands"
            commands.mkdir()
            log = root / "cmake.log"
            cmake = commands / "cmake"
            cmake.write_text('#!/bin/sh\nprintf "%s\\n" "$@" >> "$BUILD_TEST_LOG"\n')
            cmake.chmod(0o755)
            environment = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                               BUILD_TEST_LOG=str(log))
            result = subprocess.run(["sh", str(REPO / "scripts/build.sh"), str(root / "build")],
                                    env=environment, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("-DCMAKE_BUILD_TYPE=Release", log.read_text().splitlines())

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
            result = subprocess.run(["sh", str(REPO / "scripts/compile_runtime.sh"),
                                     "linux", str(REPO), str(output), str(output / "progress")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            arguments = log.read_text()
            self.assertIn("-DBUILD_EDITOR=OFF", arguments)
            self.assertNotIn("CMAKE_TOOLCHAIN_FILE", arguments)
            self.assertIn("--target runtime_app", arguments)
            self.assertTrue((output / "platform-builds/linux/apps/assets/shaders").is_dir())
            self.assertTrue((output / "progress").read_text().startswith("100|"))

    def test_single_linux_dependencies_are_configured_as_linux(self):
        with tempfile.TemporaryDirectory(prefix="bengine single linux ") as directory:
            root = Path(directory)
            commands = root / "commands"
            commands.mkdir()
            toolchains = root / "toolchains"
            zig = toolchains / "zig-0.14.1/zig"
            zig.parent.mkdir(parents=True)
            zig.write_text("#!/bin/sh\nexit 0\n")
            zig.chmod(0o755)
            log = root / "commands.log"
            for name, body in {
                "cmake": 'printf "%s\\n" "$*" >> "$BUILD_TEST_LOG"\n',
                "vcpkg": 'printf "%s\\n" "$*" >> "$BUILD_TEST_LOG"\n',
                "pkg-config": "exit 0\n",
                "nasm": "exit 0\n",
                "curl": "exit 91\n",
                "git": "exit 92\n",
            }.items():
                command = commands / name
                command.write_text("#!/bin/sh\n" + body)
                command.chmod(0o755)
            output = root / "output"
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       BUILD_TEST_LOG=str(log), VCPKG_ROOT=str(commands),
                       BENGINE_TOOLCHAIN_ROOT=str(toolchains))
            result = subprocess.run(["sh", str(REPO / "scripts/compile_runtime.sh"),
                                     "linux", str(REPO), str(output), str(output / "progress"),
                                     "", "single"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            triplet = toolchains / "triplets/x64-zig-linux.cmake"
            self.assertIn("set(VCPKG_CMAKE_SYSTEM_NAME Linux)", triplet.read_text())
            self.assertIn("-DBENGINE_SINGLE_FILE_RUNTIME=ON", log.read_text())
            self.assertIn("--overlay-ports=" + str(REPO / "cmake/vcpkg-ports"), log.read_text())

    def test_cmake_runtime_only_does_not_require_editor_sources_or_png(self):
        with tempfile.TemporaryDirectory(prefix="bengine cmake ") as directory:
            root = Path(directory)
            apps = root / "apps"
            (apps / "project").mkdir(parents=True)
            shutil.copyfile(REPO / "apps/CMakeLists.txt", apps / "CMakeLists.txt")
            (apps / "runtime_app.cpp").write_text("int main() {}")
            (apps / "project/project_persistence_system.cpp").write_text("")
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

    def test_game_export_has_clean_output_and_project_assets(self):
        import json
        with tempfile.TemporaryDirectory(prefix="bengine game export ") as directory:
            root = Path(directory)
            project = root / "My Game"
            (project / "assets").mkdir(parents=True)
            (project / "scripts").mkdir()
            (project / "assets/texture.png").write_bytes(b"texture")
            (project / "scripts/play.lua").write_text("function setup() end")
            (project / "project.bengine").write_text(json.dumps({
                "format": "bubble-engine-project", "version": 1, "scene": "main.scene"}))
            (project / "main.scene").write_text(json.dumps({
                "format": "bubble-engine-scene", "version": 1, "entities": []}))
            (project / "editor-ui.cfg").write_text("editor only")
            (project / "build").mkdir()
            (project / "build/CMakeCache.txt").write_text("not game data")
            commands = root / "commands"
            commands.mkdir()
            real_cmake = shutil.which("cmake")
            cmake = commands / "cmake"
            cmake.write_text('#!/bin/sh\n'
                'if [ "$1" = -S ] && [ "$2" = "$GAME_TEST_REPO" ]; then\n'
                ' printf "runtime configure\\n" >> "$GAME_TEST_COUNT"; mkdir -p "$4/apps"; cp /bin/true "$4/apps/runtime_app"; exit 0; fi\n'
                'if [ "$1" = --build ] && [ "${4:-}" = runtime_app ]; then exit 0; fi\n'
                'exec "$GAME_TEST_CMAKE" "$@"\n')
            cmake.chmod(0o755)
            pkg_config = commands / "pkg-config"
            pkg_config.write_text("#!/bin/sh\nexit 0\n")
            pkg_config.chmod(0o755)
            output = root / "export"
            progress = root / "progress"
            workspace = root / "workspace"
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       GAME_TEST_CMAKE=real_cmake, GAME_TEST_REPO=str(REPO), BENGINE_TEMPLATE_CACHE=str(root / "templates"), GAME_TEST_COUNT=str(root / "runtime-count"))
            result = subprocess.run(["sh", str(REPO / "scripts/platform_build.sh"), "linux",
                                     str(REPO), str(output), str(progress),
                                     str(project / "project.bengine"), str(workspace)],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            game = output / "linux"
            self.assertTrue((game / "game.json").is_file())
            manifest = json.loads((game / "game.json").read_text())
            self.assertTrue((game / manifest["project"]).is_file())
            self.assertTrue((game / "game/assets/texture.png").is_file())
            self.assertTrue((game / "game/scripts/play.lua").is_file())
            self.assertFalse((game / "game/editor-ui.cfg").exists())
            self.assertFalse((game / "game/build").exists())
            self.assertFalse(list(output.rglob("CMakeCache.txt")))
            self.assertFalse(list(output.rglob("*.log")))
            self.assertEqual({p.name for p in output.iterdir()}, {"linux"})
            other_project = root / "Other Game"
            shutil.copytree(project, other_project)
            other_output = root / "another export"
            result = subprocess.run(["sh", str(REPO / "scripts/platform_build.sh"), "linux",
                str(REPO), str(other_output), str(progress), str(other_project / "project.bengine"),
                str(root / "second workspace"), "folder", "", str(workspace / "packager/game_packager")],
                env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Using precompiled runtime template", result.stdout)
            self.assertEqual((root / "runtime-count").read_text().count("runtime configure"), 1)
            self.assertTrue((other_output / "linux/game.json").is_file())

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
                    str(REPO / "scripts/compile_runtime.ps1"), "-Target", "windows",
                    "-RepositoryRoot", str(REPO), "-BuildDirectory", str(output),
                    "-ProgressFile", str(output / "progress")]
            result = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Visual Studio 17 2022", log.read_text())
            self.assertTrue((release.parent / "dependency.dll").is_file())
            self.assertTrue((release.parent / "assets/shaders").is_dir())
            self.assertTrue((output / "progress").read_text().startswith("100|"))
            single = subprocess.run(args + ["-Format", "single"], env=env, capture_output=True, text=True)
            self.assertEqual(single.returncode, 0, single.stdout + single.stderr)
            self.assertIn("-DVCPKG_TARGET_TRIPLET=x64-windows-static", log.read_text())
            self.assertIn("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>", log.read_text())
            # A failed configure must propagate and must not report success.
            (commands / "cmake").write_text("#!/bin/sh\nexit 17\n")
            result = subprocess.run(args, env=env, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((output / "progress").read_text().startswith("100|"))

    def test_windows_game_wrapper_exports_clean_game_with_dlls(self):
        import json
        import hashlib
        pwsh = self.powershell()
        with tempfile.TemporaryDirectory(prefix="bengine windows game ") as directory:
            root = Path(directory)
            native = root / "native-tool"
            subprocess.run(["cmake", "-S", str(REPO / "tools/game_packager"), "-B", str(native)],
                           check=True, capture_output=True, text=True)
            subprocess.run(["cmake", "--build", str(native), "--parallel", "2"],
                           check=True, capture_output=True, text=True)
            project = root / "My Game"
            project.mkdir()
            (project / "project.bengine").write_text(json.dumps({
                "format": "bubble-engine-project", "version": 1, "scene": "main.scene"}))
            (project / "main.scene").write_text(json.dumps({
                "format": "bubble-engine-scene", "version": 1, "entities": []}))
            commands = root / "commands"
            commands.mkdir()
            host_log = root / "host-tools.log"
            for name in ("cmake", "vcpkg.exe"):
                script = commands / name
                script.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$BUILD_TEST_LOG"\nexit 0\n')
                script.chmod(0o755)
            output = root / "export"
            workspace_key = f"{REPO}|{project / 'project.bengine'}|{output}|windows"
            digest = hashlib.sha256(workspace_key.encode()).hexdigest().upper()[:16]
            workspace = root / "localappdata/cpp-bengine/game-builds" / digest
            packager = workspace / "packager/Release/game_packager.exe"
            packager.parent.mkdir(parents=True)
            packager.write_text('#!/bin/sh\nexec "' + str(native / "game_packager") + '" "$@"\n')
            packager.chmod(0o755)
            key = subprocess.check_output([str(native / "game_packager"), "template-key", str(REPO), "windows", "folder"], text=True).strip()
            release = workspace / ("runtime-folder-" + key) / "platform-builds/windows/apps/Release"
            release.mkdir(parents=True)
            (release / "runtime_app.exe").write_text("test executable")
            (release / "assimp.dll").write_text("test dll")
            (release / "runtime_app.pdb").write_text("debug")
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"],
                       VCPKG_ROOT=str(commands), BENGINE_TEMPLATE_CACHE=str(root / "templates"),
                       BUILD_TEST_LOG=str(host_log), LOCALAPPDATA=str(root / "localappdata"))
            result = subprocess.run([pwsh, "-NoLogo", "-NoProfile", "-File",
                str(REPO / "scripts/platform_build.ps1"), "-Target", "windows",
                "-RepositoryRoot", str(REPO), "-BuildDirectory", str(output),
                "-ProgressFile", str(workspace / "progress"),
                "-ProjectFile", str(project / "project.bengine")],
                env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("zlib:x64-windows-static", host_log.read_text())
            self.assertIn("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>", host_log.read_text())
            self.assertIn("-DVCPKG_TARGET_TRIPLET=x64-windows-static", host_log.read_text())
            self.assertIn("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>", host_log.read_text())
            self.assertTrue((output / "windows/My_Game.exe").is_file())
            self.assertTrue((output / "windows/assimp.dll").is_file())
            self.assertTrue((output / "windows/game.json").is_file())
            self.assertFalse(list(output.rglob("*.pdb")))
            self.assertFalse(list(output.rglob("*.log")))
            self.assertFalse(list(output.rglob("CMakeCache.txt")))
            self.assertTrue((workspace / "progress").read_text().startswith("100|"))

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
                                     "-ProgressFile", str(root / "progress"),
                                     "-ProjectFile", str(root / "project.bengine"),
                                     "-Workspace", str(root / "workspace")],
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
            args = log.read_text().splitlines()
            self.assertEqual(args[:3], ["--exec", "bash", str(REPO / "scripts/platform_build.sh")])
            self.assertEqual(args[3:], ["linux", str(REPO), str(root / "output with spaces"),
                                       str(root / "progress"), str(root / "project.bengine"),
                                       str(root / "workspace"), "folder", ""])


if __name__ == "__main__":
    unittest.main()
