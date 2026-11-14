"""Launch the extracted portable release away from the checkout and without PATH tools."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

archives = list(Path(sys.argv[1]).glob('*-portable.zip'))
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
