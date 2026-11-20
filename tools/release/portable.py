"""Self-contained entry point; PyInstaller embeds the complete engine payload."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile

import updater


def environment():
    result = dict(os.environ)
    if sys.platform != 'win32' and getattr(sys, 'frozen', False):
        original = result.pop('LD_LIBRARY_PATH_ORIG', None)
        result.pop('LD_LIBRARY_PATH', None)
        if original is not None:
            result['LD_LIBRARY_PATH'] = original
    return result


def verify(root):
    inventory = json.loads((root / 'portable-inventory.json').read_text())
    for name, digest in inventory.items():
        path = root / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'Missing or damaged bundled file: {name}')
    platform = 'windows' if sys.platform == 'win32' else 'linux'
    suffix = '.exe' if sys.platform == 'win32' else ''
    template = root / 'source/export_templates' / platform / 'folder'
    manifest = json.loads((template / 'template.json').read_text())
    subprocess.run([str(root / ('game_packager' + suffix)), 'template-validate',
                    str(template), manifest['engine_key'], platform, 'folder'],
                   env=environment(), check=True)


def browse(initial, directory):
    import tkinter
    from tkinter import filedialog
    window = tkinter.Tk()
    window.withdraw()
    try:
        folder = Path(initial).expanduser()
        if not folder.is_dir():
            folder = folder.parent
        options = dict(parent=window, title='Bubble Engine', initialdir=str(folder))
        selected = (filedialog.askdirectory(**options) if directory else
                    filedialog.askopenfilename(**options, filetypes=[
                        ('Bubble Engine', '*.bengine *.scene *.world'), ('All files', '*')]))
        if selected:
            print(selected, flush=True)
        return 0 if selected else 1
    finally:
        window.destroy()


def run(root, arguments):
    if sys.platform == 'win32':
        import ctypes
        ctypes.windll.kernel32.SetDllDirectoryW(None)
    if len(arguments) == 3 and arguments[0] == '--browse':
        return browse(arguments[1], arguments[2] == 'directory')
    if arguments == ['--verify-package']:
        import tkinter
        tkinter.Tcl()
        verify(root)
        return 0
    if arguments == ['--smoke-test']:
        if sys.platform == 'win32' and os.environ.get('BENGINE_SMOKE_OPENGL'):
            driver = Path(os.environ['BENGINE_SMOKE_OPENGL'])
            for name in ('opengl32.dll', 'libgallium_wgl.dll'):
                shutil.copy2(driver / name, root / name)
        with tempfile.TemporaryDirectory(prefix='bubble-smoke-') as directory:
            state = Path(directory)
            ready = state / 'ready'
            process = updater.launch(root, state, [], ready)
            try:
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    if process.poll() is not None:
                        raise RuntimeError('Portable editor exited before becoming ready')
                    if ready.is_file():
                        return 0
                    time.sleep(0.1)
                raise RuntimeError('Portable editor did not become ready within 30 seconds')
            finally:
                if process.poll() is None:
                    process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    if len(arguments) == 2 and arguments[0] == '--project':
        arguments = ['--project', str(Path(arguments[1]).resolve())]
    if sys.platform == 'win32':
        state = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'BubbleEngine'
    else:
        state = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share')) / 'bubble-engine'
    state.mkdir(parents=True, exist_ok=True)
    os.environ['BENGINE_PORTABLE'] = '1'
    os.environ['BENGINE_FILE_DIALOG'] = sys.executable
    return updater.launch(root, state, arguments).wait()


def main(arguments):
    payload = Path(sys._MEIPASS) / 'engine.zip'
    with zipfile.ZipFile(payload) as archive:
        version = json.loads(archive.read('engine/release.json'))['engine_version']
    with tempfile.TemporaryDirectory(prefix='bubble-engine-') as directory:
        root = updater.extract(payload, Path(directory), 'engine', version)
        return run(root, arguments)


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'Cannot open Bubble Engine: {error}', file=sys.stderr)
        sys.exit(1)
