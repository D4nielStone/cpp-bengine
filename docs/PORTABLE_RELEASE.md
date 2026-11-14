# Portable releases

Download `bubble-engine-vX.Y.Z-<platform>-portable.zip`, extract it and open
`BubbleEngine` on Linux or `BubbleEngine.exe` on Windows. The executable contains
the editor, runtime, game packager, engine assets, shared libraries and a private
Python/Tcl/Tk runtime. Python, CMake, a compiler, Zenity and KDialog do not need to
be installed. Files are unpacked into a private temporary directory for the
session and removed after the editor exits. Keep user projects outside that directory.
The first launch therefore needs temporary disk space and an executable temporary
filesystem. Linux requires a supported desktop, system C/C++ runtime and graphics
drivers (the build baseline is Ubuntu 22.04); drivers are not bundled.

Preferences and recent projects persist in `%LOCALAPPDATA%/BubbleEngine` on Windows
or `$XDG_DATA_HOME/bubble-engine` (`~/.local/share/bubble-engine` by default) on Linux.
Startup is offline and does not wait for an update check. Download a new portable
release to update it. The existing folder archive and its managed updater remain
available separately for compatibility.

Folder exports for the release's own OS use the bundled precompiled template,
including its dependencies. Single-executable exports and cross-compilation are
not supplied by the portable release; missing templates produce an explicit error
instead of downloading a toolchain. Development builds retain their existing
compilation fallback. Export scripts use the OS shell (PowerShell on Windows).

## Release validation

`package.py` rejects non-distribution builds. It creates and validates the native
runtime template and includes MSVC redistributable DLLs on Windows. The portable
payload is embedded as a ZIP so the freezer cannot rewrite engine binaries or
invalidate template hashes. Before publishing, the package is executed with an
empty `PATH` and `--verify-package`: it verifies SHA-256 hashes of the payload,
loads the bundled Tcl runtime and runs the bundled template validator.

The Linux workflow also extracts the final portable ZIP outside the checkout and
runs `--smoke-test` under Xvfb/software OpenGL with an empty `PATH`. The editor must
signal first-frame readiness within 30 seconds. This does not replace testing on
physical Windows/Linux desktops or cover every driver combination.
