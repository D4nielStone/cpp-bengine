"""Validate release tags and package relocatable, native engine builds."""
import argparse
import hashlib
import json
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


def package(build, output, tag, library_dir=None):
    version = validate_version(tag)
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
        shutil.copy2(ROOT / 'LICENSE', stage / 'LICENSE')
        (stage / 'release.json').write_text(json.dumps({'engine_version': version, 'platform': platform}, indent=2))
        if windows:
            (stage / 'BubbleEngine.cmd').write_text('@echo off\nset "BENGINE_HOME=%~dp0"\ncd /d "%~dp0"\n"%~dp0editor_app.exe" %*\n')
        else:
            launcher = stage / 'BubbleEngine.sh'
            launcher.write_text('#!/bin/sh\nBENGINE_HOME=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport BENGINE_HOME\ncd "$BENGINE_HOME" || exit 1\nexec "$BENGINE_HOME/editor_app" "$@"\n')
            launcher.chmod(0o755)
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
    args = parser.parse_args()
    validate_version(args.version)
    if not args.validate_only:
        package(args.build, args.output, args.version, args.library_dir)


if __name__ == '__main__':
    main()
