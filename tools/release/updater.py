"""Launch a portable installation and commit updates after editor startup."""
import contextlib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import urllib.request
import uuid
import zipfile

WINDOWS = sys.platform == 'win32'
PLATFORM = 'windows-x64' if WINDOWS else 'linux-x64'
SUFFIX = '.exe' if WINDOWS else ''
REPOSITORY = 'D4nielStone/cpp-bengine'
VERSION = re.compile(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)')


def version(value):
    match = VERSION.fullmatch(value)
    if not match:
        raise ValueError('Invalid release version')
    return tuple(map(int, match.groups()))


def request(url):
    return urllib.request.urlopen(
        urllib.request.Request(url, headers={'User-Agent': 'BubbleEngine-Updater'}), timeout=5)


def download(current, staging, report):
    with request(f'https://api.github.com/repos/{REPOSITORY}/releases/latest') as response:
        release = json.loads(response.read(1024 * 1024))
    tag = release['tag_name']
    if release['draft'] or release['prerelease'] or not tag.startswith('v'):
        return None
    if version(tag[1:]) <= version(current):
        return None
    name = f'bubble-engine-{tag}-{PLATFORM}'
    assets = {asset['name']: asset['size'] for asset in release['assets']}
    if name + '.zip' not in assets or name + '.sha256' not in assets:
        return None
    base = f'https://github.com/{REPOSITORY}/releases/download/{tag}/{name}'
    with request(base + '.sha256') as response:
        checksum = response.read(1024).decode('ascii').split()
    if len(checksum) != 2 or checksum[1] != name + '.zip' or not re.fullmatch('[a-f0-9]{64}', checksum[0]):
        raise ValueError('Invalid release checksum')
    report('Downloading update ' + tag + '...')
    archive = staging / 'release.zip'
    digest = hashlib.sha256()
    size = assets[name + '.zip']
    if not 0 < size <= 1024 ** 3:
        raise ValueError('Invalid release download size')
    remaining = size
    deadline = time.monotonic() + 120
    with request(base + '.zip') as response, archive.open('wb') as output:
        while block := response.read(256 * 1024):
            remaining -= len(block)
            if remaining < 0 or time.monotonic() > deadline:
                raise ValueError('Release download exceeded its limits')
            digest.update(block)
            output.write(block)
    if remaining or digest.hexdigest() != checksum[0]:
        raise ValueError('Release checksum mismatch')
    return extract(archive, staging, name, tag[1:])


def extract(archive, staging, name, expected_version):
    root = staging / name
    with zipfile.ZipFile(archive) as package:
        entries = package.infolist()
        if sum(entry.file_size for entry in entries) > 2 * 1024 ** 3:
            raise ValueError('Release archive exceeds extraction limit')
        for entry in entries:
            parts = PurePosixPath(entry.filename).parts
            mode = entry.external_attr >> 16
            if (len(parts) < 2 or parts[0] != name or '..' in parts
                    or '\\' in entry.filename or ':' in entry.filename
                    or any(part.endswith(('.', ' ')) for part in parts)
                    or stat.S_IFMT(mode) not in (0, stat.S_IFREG)):
                raise ValueError('Invalid release archive path')
            target = staging.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with package.open(entry) as source, target.open('xb') as output:
                shutil.copyfileobj(source, output)
            if not WINDOWS:
                target.chmod(0o755 if mode & 0o111 else 0o644)
    manifest = json.loads((root / 'release.json').read_text())
    if manifest != {'engine_version': expected_version, 'platform': PLATFORM}:
        raise ValueError('Unexpected release manifest')
    for binary in ('editor_app', 'runtime_app', 'game_packager'):
        if not (root / (binary + SUFFIX)).is_file():
            raise ValueError(f'Missing release binary {binary}')
    return root


@contextlib.contextmanager
def installation_lock(path):
    with path.open('a+b') as lock:
        if WINDOWS:
            import msvcrt
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        yield


def launch(root, installation, arguments, ready=None):
    environment = dict(os.environ, BENGINE_HOME=str(root),
                       BENGINE_INSTALLATION=str(installation), BENGINE_MANAGED_UPDATE='1')
    environment.pop('BENGINE_UPDATE_READY', None)
    if not WINDOWS and getattr(sys, 'frozen', False):
        original = environment.pop('LD_LIBRARY_PATH_ORIG', None)
        environment.pop('LD_LIBRARY_PATH', None)
        if original is not None:
            environment['LD_LIBRARY_PATH'] = original
    if ready is not None:
        environment['BENGINE_UPDATE_READY'] = str(ready)
    return subprocess.Popen([str(root / ('editor_app' + SUFFIX)), *arguments],
                            cwd=root, env=environment)


def try_update(candidate, installation, arguments, ready, active):
    process = launch(candidate, installation, arguments, ready)
    committed = False
    try:
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if process.poll() not in (None, 0):
                break
            if ready.is_file():
                pointer = ready.with_name('active.tmp')
                pointer.write_text(candidate.name, encoding='ascii')
                os.replace(pointer, active)
                committed = True
                return process
            if process.poll() is not None:
                break
            time.sleep(0.1)
    finally:
        if not committed:
            if process.poll() is None:
                process.kill()
            process.wait()
    return None


def selected(pointer, updates):
    if pointer.is_file():
        name = pointer.read_text(encoding='ascii')
        if re.fullmatch(r'version-[0-9a-f]{32}', name):
            candidate = updates / name
            if (candidate / ('editor_app' + SUFFIX)).is_file():
                return candidate
    return None


def run(installation, arguments):
    if len(arguments) == 2 and arguments[0] == '--project':
        arguments = ['--project', str(installation / arguments[1])]
    updates = installation / '.engine-updates'
    try:
        updates.mkdir(exist_ok=True)
    except OSError as error:
        print(f'Update unavailable: {error}', file=sys.stderr)
        return launch(installation, installation, arguments).wait()
    def report(message):
        print(message, flush=True)
        try:
            (updates / 'status').write_text(message + '\n', encoding='ascii')
        except OSError:
            pass

    active = updates / 'active'
    with installation_lock(updates / 'lock'):
        current = selected(active, updates) or installation
        pending = updates / 'pending'
        candidate = selected(pending, updates)
        try:
            with tempfile.TemporaryDirectory(dir=updates) as directory:
                staging = Path(directory)
                if candidate is not None:
                    pending.unlink()
                else:
                    report('Checking for updates...')
                    manifest = json.loads((current / 'release.json').read_text())
                    extracted = download(manifest['engine_version'], staging, report)
                    if extracted is not None:
                        candidate = updates / ('version-' + uuid.uuid4().hex)
                        extracted.rename(candidate)
                if candidate is not None and candidate != current:
                    report('Starting updated editor...')
                    process = try_update(candidate, installation, arguments, staging / 'ready', active)
                    if process is not None:
                        report('Engine updated.')
                        return process.wait()
                    report('Update failed. Previous version restored.')
                else:
                    report('Engine is up to date.')
        except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
            print(f'Update unavailable: {error}', file=sys.stderr)
            report('Update check unavailable.')
        return launch(current, installation, arguments).wait()


if __name__ == '__main__':
    try:
        if WINDOWS and getattr(sys, 'frozen', False):
            import ctypes
            ctypes.windll.kernel32.SetDllDirectoryW(None)
        sys.exit(run(Path(sys.executable).resolve().parent, sys.argv[1:]))
    except OSError as error:
        print(f'Cannot open Bubble Engine: {error}', file=sys.stderr)
        sys.exit(1)
