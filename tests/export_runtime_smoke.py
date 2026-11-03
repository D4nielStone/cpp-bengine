"""Start a minimal exported project from an unrelated directory."""
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def main():
    executable = Path(sys.argv[1]).resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="bengine exported game ") as directory:
        log_path = Path(directory) / "runtime.log"
        with log_path.open("w") as output:
            process = subprocess.Popen([str(executable)], cwd=directory,
                                       stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 20
                ready_at = None
                while time.monotonic() < deadline:
                    contents = log_path.read_text(errors="replace")
                    if process.poll() is not None:
                        raise RuntimeError(f"Runtime exited with {process.returncode}:\n{contents}")
                    if "Initialization failed:" in contents or "Update failed:" in contents:
                        raise RuntimeError(contents)
                    if ready_at is None:
                        ready_at = time.monotonic()
                    if time.monotonic() - ready_at >= 2:
                        print(f"Exported runtime started successfully: {executable.name}")
                        return
                    time.sleep(0.05)
                raise RuntimeError(
                    f"Runtime did not remain active:\n{log_path.read_text(errors='replace')}"
                )
            finally:
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == "__main__":
    main()
