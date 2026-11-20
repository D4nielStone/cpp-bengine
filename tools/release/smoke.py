"""Exercise release artifacts outside the checkout."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import zipfile


REPOSITORY = Path(__file__).resolve().parents[2]


def smoke_portable(archives):
    if not archives:
        raise SystemExit('No portable release archive found')
    for archive in archives:
        with tempfile.TemporaryDirectory(prefix='bubble portable smoke ') as directory:
            root = Path(directory)
            with zipfile.ZipFile(archive) as package:
                package.extractall(root)
            binary = root / ('BubbleEngine.exe' if sys.platform == 'win32' else 'BubbleEngine')
            binary.chmod(0o755)
            subprocess.run([str(binary), '--smoke-test'], cwd=root,
                           env=dict(os.environ, PATH='', XDG_DATA_HOME=str(root / 'state')),
                           check=True, timeout=90)


def smoke_native_update(launcher, engine, root):
    state = root / 'state'
    updates = state / 'bubble-engine/.engine-updates'
    candidate = updates / ('version-' + 'a' * 32)
    candidate.mkdir(parents=True)
    manifest = json.loads((engine / 'release.json').read_text())
    major, minor, patch = map(int, manifest['engine_version'].split('.'))
    manifest['engine_version'] = f'{major}.{minor}.{patch + 1}'
    for path in engine.iterdir():
        if path.name != 'release.json':
            (candidate / path.name).symlink_to(path)
    (candidate / 'release.json').write_text(json.dumps(manifest))
    (updates / 'pending').write_text(candidate.name, encoding='ascii')
    active = updates / 'active'
    process = subprocess.Popen([str(launcher), '--project', 'release-smoke/project.bengine'],
                               cwd=root, env=dict(os.environ, XDG_DATA_HOME=str(state), TMPDIR=str(root)),
                               start_new_session=True)
    try:
        deadline = time.monotonic() + 45
        while process.poll() is None and time.monotonic() < deadline:
            if active.is_file() and active.read_text(encoding='ascii') == candidate.name:
                return
            time.sleep(0.1)
        raise RuntimeError('The installed launcher did not confirm the trial update')
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def smoke_native(artifact):
    with tempfile.TemporaryDirectory(prefix='bubble native smoke ') as directory:
        root = Path(directory)
        installation = root / 'installed'
        installation.mkdir()
        if artifact.suffix == '.deb':
            subprocess.run(['dpkg-deb', '-x', str(artifact), str(installation)], check=True)
        else:
            with tempfile.TemporaryFile() as payload:
                subprocess.run(['rpm2cpio', str(artifact)], stdout=payload, check=True)
                payload.seek(0)
                subprocess.run(['cpio', '-id', '--quiet'], stdin=payload, cwd=installation, check=True)
        engine = installation / 'usr/lib/bubble-engine'
        launcher = installation / 'usr/bin/bubble-engine'
        if not os.access(launcher, os.X_OK) or not os.access(engine / 'engine_updater', os.X_OK):
            raise RuntimeError(f'Editor launcher or updater is missing from {artifact.name}')
        project = root / 'release-smoke'
        subprocess.run([sys.executable, str(REPOSITORY / 'tests/create_smoke_project.py'), str(project)], check=True)
        smoke_native_update(launcher, engine, root)
        commands = root / 'commands'
        commands.mkdir()
        for command in ('cp', 'dirname', 'mkdir'):
            (commands / command).symlink_to(shutil.which(command))
        source = engine / 'source'
        export = root / 'export'
        environment = dict(os.environ, PATH=str(commands), BENGINE_EXPORT_ALLOW_BUILD='0',
                           BENGINE_TEMPLATE_CACHE=str(root / 'empty-cache'))
        environment.pop('BENGINE_EXPORT_TEMPLATES', None)
        subprocess.run(['/bin/sh', str(source / 'scripts/platform_build.sh'), 'linux', str(source),
                        str(export), str(root / 'progress'), str(project / 'project.bengine'),
                        str(root / 'work'), 'folder', '', str(engine / 'game_packager')],
                       cwd=root, env=environment,
                       check=True, timeout=60)
        subprocess.run([sys.executable, str(REPOSITORY / 'tests/export_runtime_smoke.py'),
                        str(export / 'linux/release-smoke')], cwd=root, check=True, timeout=60)


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--native-linux', action='store_true')
    args = parser.parse_args()
    if args.native_linux:
        for pattern in ('*.deb', '*.rpm'):
            archives = list(args.directory.glob(pattern))
            if not archives:
                raise SystemExit(f'No {pattern} release artifact found')
            for archive in archives:
                smoke_native(archive.resolve())
    else:
        smoke_portable(list(args.directory.glob('*-portable.zip')))


if __name__ == '__main__':
    main()
