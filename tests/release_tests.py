import importlib.util
from pathlib import Path
import unittest
from unittest import mock
import tempfile
import zipfile
import sys
sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('release_package', Path(__file__).resolve().parents[1] / 'tools/release/package.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

updater_spec = importlib.util.spec_from_file_location('release_updater', module.ROOT / 'tools/release/updater.py')
updater = importlib.util.module_from_spec(updater_spec)
updater_spec.loader.exec_module(updater)


portable_spec = importlib.util.spec_from_file_location('release_portable', module.ROOT / 'tools/release/portable.py')
portable = importlib.util.module_from_spec(portable_spec)
with mock.patch.dict(sys.modules, {'updater': updater}):
    portable_spec.loader.exec_module(portable)


class PortableTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == 'linux', 'POSIX runtime build guard')
    def test_portable_does_not_bootstrap_external_toolchains(self):
        import os
        import subprocess
        result = subprocess.run(['/bin/sh', str(module.ROOT / 'scripts/compile_runtime.sh')],
                                env=dict(os.environ, BENGINE_PORTABLE='1', PATH=''),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 6)
        self.assertIn('precompiled runtime', result.stderr)


    def test_embedded_zip_extracts_and_is_removed_after_editor_exits(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with zipfile.ZipFile(root / 'engine.zip', 'w') as archive:
                archive.writestr('engine/release.json', json.dumps({
                    'engine_version': '1.2.3', 'platform': updater.PLATFORM}))
                for name in ('editor_app', 'runtime_app', 'game_packager'):
                    archive.writestr('engine/' + name + updater.SUFFIX, b'binary')
            extracted = []
            def launch(payload, arguments):
                extracted.append(payload)
                self.assertTrue((payload / ('editor_app' + updater.SUFFIX)).is_file())
                self.assertEqual(arguments, ['--verify-package'])
                return 7
            with mock.patch.object(sys, '_MEIPASS', str(root), create=True), \
                 mock.patch.object(portable, 'run', side_effect=launch):
                self.assertEqual(portable.main(['--verify-package']), 7)
            self.assertFalse(extracted[0].exists())

    def test_smoke_detects_startup_failure_and_reaps_process(self):
        process = mock.Mock()
        process.poll.return_value = 1
        with mock.patch.object(updater, 'launch', return_value=process):
            with self.assertRaisesRegex(RuntimeError, 'before becoming ready'):
                portable.run(Path('/payload'), ['--smoke-test'])
        process.wait.assert_called_once_with(timeout=10)


    def test_launch_preserves_caller_project_and_uses_persistent_state(self):
        import os
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            process = mock.Mock()
            process.wait.return_value = 0
            with mock.patch.object(sys, '_MEIPASS', str(root), create=True), \
                 mock.patch.dict(os.environ, {'LOCALAPPDATA': str(root), 'XDG_DATA_HOME': str(root)}), \
                 mock.patch.object(updater, 'launch', return_value=process) as launch:
                self.assertEqual(portable.run(root / 'engine', ['--project', 'my project/project.bengine']), 0)
                payload, state, arguments = launch.call_args.args
                self.assertEqual(payload, root / 'engine')
                self.assertNotEqual(state, payload)
                self.assertTrue(state.is_dir())
                self.assertEqual(arguments, ['--project', str(Path('my project/project.bengine').resolve())])
                self.assertEqual(os.environ['BENGINE_FILE_DIALOG'], sys.executable)

    def test_verify_rejects_corrupt_payload_before_running_tools(self):
        import hashlib
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'asset').write_bytes(b'changed')
            (root / 'portable-inventory.json').write_text(json.dumps({
                'asset': hashlib.sha256(b'original').hexdigest()}))
            with mock.patch.object(portable.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'damaged bundled file'):
                    portable.verify(root)
                run.assert_not_called()

    def test_verify_checks_precompiled_template(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'portable-inventory.json').write_text('{}')
            target = 'windows' if sys.platform == 'win32' else 'linux'
            template = root / 'source/export_templates' / target / 'folder'
            template.mkdir(parents=True)
            (template / 'template.json').write_text(json.dumps({'engine_key': 'test-key'}))
            with mock.patch.object(portable.subprocess, 'run') as run:
                portable.verify(root)
                self.assertEqual(run.call_args.args[0][1:],
                                 ['template-validate', str(template), 'test-key', target, 'folder'])

    @unittest.skipIf(sys.platform == 'win32', 'Linux loader environment')
    def test_frozen_environment_does_not_leak_python_libraries_to_engine(self):
        import os
        with mock.patch.object(sys, 'frozen', True, create=True), \
             mock.patch.dict(os.environ, {'LD_LIBRARY_PATH': '/temporary/python',
                                          'LD_LIBRARY_PATH_ORIG': '/user/libraries'}):
            result = portable.environment()
            self.assertEqual(result['LD_LIBRARY_PATH'], '/user/libraries')
            self.assertNotIn('LD_LIBRARY_PATH_ORIG', result)


class UpdateTests(unittest.TestCase):
    def test_trial_changes_active_version_only_after_readiness(self):
        for ready_to_commit in (False, True):
            with self.subTest(ready=ready_to_commit), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                active = root / 'active'
                active.write_text('old')
                ready = root / 'ready'
                if ready_to_commit:
                    ready.touch()
                process = mock.Mock()
                process.poll.return_value = None if ready_to_commit else 1
                candidate = root / 'new'
                with mock.patch.object(updater, 'launch', return_value=process):
                    result = updater.try_update(candidate, root, [], ready, active)
                self.assertEqual(active.read_text(), 'new' if ready_to_commit else 'old')
                if ready_to_commit:
                    self.assertIs(result, process)
                    process.kill.assert_not_called()
                else:
                    self.assertIsNone(result)
                    process.wait.assert_called_once()

    def test_downloaded_update_is_tried_during_the_same_launch(self):
        for starts in (False, True):
            with self.subTest(starts=starts), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'release.json').write_text('{"engine_version": "1.2.3"}')
                process = mock.Mock()
                process.wait.return_value = 0

                def download(current, staging, report):
                    candidate = staging / 'release'
                    candidate.mkdir()
                    return candidate

                with mock.patch.object(updater, 'download', side_effect=download) as fetch, \
                     mock.patch.object(updater, 'try_update', return_value=process if starts else None) as trial, \
                     mock.patch.object(updater, 'launch', return_value=process) as fallback:
                    self.assertEqual(updater.run(root, []), 0)
                fetch.assert_called_once()
                trial.assert_called_once()
                self.assertTrue(trial.call_args.args[0].is_dir())
                if starts:
                    fallback.assert_not_called()
                else:
                    fallback.assert_called_once_with(root, root, [])

    def test_archive_cannot_write_outside_version_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / 'package.zip'
            with zipfile.ZipFile(archive, 'w') as package:
                package.writestr('release/../user-project', 'unexpected')
            with self.assertRaises(ValueError):
                updater.extract(archive, root, 'release', '1.2.3')
            self.assertFalse((root / 'user-project').exists())


class ReleaseTests(unittest.TestCase):
    def test_development_binaries_cannot_be_published(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'CMakeCache.txt').write_text('BENGINE_DISTRIBUTION_BUILD:BOOL=OFF\n')
            with mock.patch.object(module, 'validate_version', return_value='1.2.3'):
                with self.assertRaisesRegex(ValueError, 'DISTRIBUTION_BUILD=ON'):
                    module.package(root, root / 'release', 'v1.2.3')
            self.assertFalse((root / 'release').exists())


    def test_release_changelog_includes_tag_preview(self):
        workflow = (module.ROOT / '.github/workflows/release_installer.yml').read_text()
        self.assertIn(
            '![Bubble Engine preview](https://raw.githubusercontent.com/%s/%s/preview.jpg)',
            workflow)
        self.assertIn('"$GITHUB_REPOSITORY" "$GITHUB_REF_NAME" > release/CHANGELOG.md',
                      workflow)

    @unittest.skipUnless(sys.platform == 'linux', 'Linux library bundling')
    def test_host_cpp_runtime_is_not_bundled(self):
        import shutil
        import subprocess
        import tempfile
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='bengine libraries ') as directory:
            root = Path(directory)
            source = root / 'main.cpp'
            source.write_text('#include <iostream>\nint main() { std::cout << "runtime"; }\n')
            binary = root / 'program'
            subprocess.run([compiler, str(source), '-o', str(binary)], check=True, capture_output=True)
            libraries = root / 'lib'
            subprocess.run(['cmake', f'-DBINARY={binary}', f'-DDESTINATION={libraries}',
                            '-P', str(module.ROOT / 'scripts/bundle_engine_libraries.cmake')],
                           check=True, capture_output=True)
            self.assertFalse(list(libraries.glob('libstdc++*')))
            self.assertFalse(list(libraries.glob('libgcc_s*')))

    def test_tag_matches_cmake(self):
        import re
        version = re.search(r'VERSION\s+(\d+\.\d+\.\d+)', (module.ROOT / 'CMakeLists.txt').read_text())[1]
        self.assertEqual(module.validate_version('v' + version), version)

    def test_rejects_invalid_and_mismatched_tags(self):
        for tag in ('v999.0.0', '0.1.0', 'v01.1.0', 'v0.1', 'v0.1.0-rc1', 'v0.1.0+build'):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                module.validate_version(tag)


if __name__ == '__main__':
    unittest.main()
