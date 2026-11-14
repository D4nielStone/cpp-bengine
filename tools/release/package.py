"""Validate release tags and package relocatable, native engine builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def validate_version(tag):
    match = re.search(r'project\(\s*BubbleEngine\s+VERSION\s+(\d+\.\d+\.\d+)\b',
                      (ROOT / 'CMakeLists.txt').read_text())
    if not match or not re.fullmatch(r'v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', tag):
        raise ValueError('Expected a stable SemVer tag: vMAJOR.MINOR.PATCH')
    if tag[1:] != match[1]:
        raise ValueError(f'Tag {tag} does not match CMake version {match[1]}')
    return match[1]


def package(build, output, tag, library_dir=None, updater=None, portable=False):
    version = validate_version(tag)
    cache = (build / 'CMakeCache.txt').read_text()
    if not re.search(r'^BENGINE_DISTRIBUTION_BUILD:BOOL=ON$', cache, re.MULTILINE):
        raise ValueError('Release packaging requires BENGINE_DISTRIBUTION_BUILD=ON')
    windows = sys.platform == 'win32'
    platform = 'windows-x64' if windows else 'linux-x64'
    name = f'bubble-engine-{tag}-{platform}'
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as work:
        stage = Path(work) / name
        stage.mkdir()
        suffix = '.exe' if windows else ''
        for executable in ('editor_app', 'runtime_app', 'game_packager'):
            source = build / 'apps' / (executable + suffix)
            if windows and not source.exists():
                source = build / 'apps' / 'Release' / (executable + suffix)
            if not source.is_file():
                raise FileNotFoundError(source)
            shutil.copy2(source, stage / source.name)
            command = ['cmake', f'-DBINARY={source.resolve()}',
                       f'-DDESTINATION={stage if windows else stage / "lib"}']
            if library_dir:
                command.append(f'-DLIBRARY_DIRECTORIES={library_dir.resolve()}')
            subprocess.run(command + ['-P', str(ROOT / 'scripts/bundle_engine_libraries.cmake')], check=True)
        if windows:
            runtimes = (build / 'msvc-runtime-libraries.txt').read_text().strip().split(';')
            if not runtimes or not all(runtimes):
                raise ValueError('MSVC runtime inventory is missing')
            for runtime in runtimes:
                shutil.copy2(runtime, stage / Path(runtime).name)
        shutil.copytree(ROOT / 'external/cpp-bgui/assets', stage / 'assets')
        shutil.copytree(ROOT / 'commons/assets', stage / 'assets', dirs_exist_ok=True)
        # Runtime export uses the existing source build architecture.
        source_root = stage / 'source'
        source_root.mkdir()
        tracked = subprocess.check_output(['git', 'ls-files', '--recurse-submodules', '-z'], cwd=ROOT).split(b'\0')
        tracked += subprocess.check_output(['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT).split(b'\0')
        for item in tracked:
            if not item:
                continue
            relative = Path(item.decode())
            if relative.parts[0] not in ('apps', 'commons', 'external', 'scripts', 'tools', 'cmake', 'tests') and relative.name not in ('CMakeLists.txt', 'LICENSE'):
                continue
            source = ROOT / relative
            if source.is_symlink():
                raise ValueError(f'Unexpected source symlink: {relative}')
            if source.is_file():
                destination = source_root / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
        packager = stage / ('game_packager' + suffix)
        target = 'windows' if windows else 'linux'
        template = source_root / 'export_templates' / target / 'folder'
        if not windows:
            shutil.copytree(stage / 'lib', template / 'lib')
        key = subprocess.check_output([str(packager), 'template-key', str(source_root),
                                       target, 'folder'], text=True).strip()
        subprocess.run([str(packager), 'template-create', str(stage / ('runtime_app' + suffix)),
                        str(template), key, target, 'folder'], check=True)
        subprocess.run([str(packager), 'template-validate', str(template), key,
                        target, 'folder'], check=True)
        shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
        (stage / 'release.json').write_text(json.dumps({'engine_version': version, 'platform': platform}, indent=2))
        shutil.copy2(updater, stage / ('engine_updater' + suffix))
        if windows:
            (stage / 'BubbleEngine.cmd').write_text('@echo off\n"%~dp0engine_updater.exe" %*\n')
        else:
            launcher = stage / 'BubbleEngine.sh'
            launcher.write_text('#!/bin/sh\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1\nexec "$root/engine_updater" "$@"\n')
            launcher.chmod(0o755)
        if portable:
            inventory = {path.relative_to(stage).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in sorted(stage.rglob('*')) if path.is_file()}
            (stage / 'portable-inventory.json').write_text(json.dumps(inventory, sort_keys=True))
            payload = Path(work) / 'engine.zip'
            with zipfile.ZipFile(payload, 'w', zipfile.ZIP_DEFLATED) as bundle:
                for path in sorted(stage.rglob('*')):
                    if path.is_file():
                        bundle.write(path, Path('engine') / path.relative_to(stage))
            portable_dist = Path(work) / 'portable'
            subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
                            '--onefile', '--name', 'BubbleEngine', '--hidden-import', 'tkinter.filedialog',
                            '--distpath', str(portable_dist), '--workpath', str(Path(work) / 'pyinstaller'),
                            '--specpath', work, '--add-data', str(payload) + ':.',
                            str(ROOT / 'tools/release/portable.py')], check=True)
            portable_binary = portable_dist / ('BubbleEngine' + suffix)
            subprocess.run([str(portable_binary), '--verify-package'], cwd=work,
                           env=dict(os.environ, PATH=''), check=True)
            portable_archive = output / (name + '-portable.zip')
            with zipfile.ZipFile(portable_archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
                bundle.write(portable_binary, portable_binary.name)
            digest = hashlib.sha256(portable_archive.read_bytes()).hexdigest()
            (output / (portable_archive.stem + '.sha256')).write_text(f'{digest}  {portable_archive.name}\n')
        archive = output / (name + '.zip')
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(stage.rglob('*')):
                if path.is_file():
                    bundle.write(path, path.relative_to(stage.parent))
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        (output / (name + '.sha256')).write_text(f'{digest}  {archive.name}\n')


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--validate-only', action='store_true')
    parser.add_argument('--build', type=Path, default=Path('release-build'))
    parser.add_argument('--output', type=Path, default=Path('release'))
    parser.add_argument('--library-dir', type=Path)
    parser.add_argument('--updater', type=Path)
    parser.add_argument('--portable', action='store_true')
    args = parser.parse_args()
    validate_version(args.version)
    if not args.validate_only:
        if args.updater is None:
            parser.error('--updater is required for packaging')
        package(args.build, args.output, args.version, args.library_dir, args.updater, args.portable)


if __name__ == '__main__':
    main()
