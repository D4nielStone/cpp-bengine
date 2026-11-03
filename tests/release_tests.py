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
