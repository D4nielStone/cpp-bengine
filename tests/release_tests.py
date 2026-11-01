import importlib.util
from pathlib import Path
import unittest
import sys
sys.dont_write_bytecode = True

spec = importlib.util.spec_from_file_location('release_package', Path(__file__).resolve().parents[1] / 'tools/release/package.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ReleaseTests(unittest.TestCase):
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
