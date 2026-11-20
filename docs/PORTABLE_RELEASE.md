# Releases

## Linux packages

Each tagged release builds x86_64 `.deb` and `.rpm` packages. Download the
matching file from Releases and install it with your package manager.

```sh
sudo apt install ./bubble-engine-vX.Y.Z-linux-x64.deb
sudo dnf install ./bubble-engine-vX.Y.Z-linux-x64.rpm
```

Open **Bubble Engine** in the application menu or run `bubble-engine`. The
packages include the Linux folder-export template. The editor checks for
updates at startup and validates each download before trying it. It switches
to the downloaded version after the editor confirms startup. Preferences
and downloaded updates use `$XDG_DATA_HOME/bubble-engine`
(`~/.local/share/bubble-engine` by default). When the system package has the
same or a newer version, it takes precedence over the cached update.

The Linux release is built on Ubuntu 22.04 and needs a compatible x86_64
desktop with OpenGL drivers. Verify any downloaded ZIP, DEB or RPM using
the release's `SHA256SUMS`.

## Portable ZIP

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
release to update it. The folder ZIP also provides managed automatic updates.

Folder exports for the release's own OS use the bundled precompiled template,
including its dependencies. Single-executable exports and cross-compilation are
not supplied by the portable release; missing templates produce an explicit error
instead of downloading a toolchain. Source builds can explicitly enable
template compilation as described in the [build guide](BUILDING.md).
Export scripts use the OS shell (PowerShell on Windows).

## Release validation

`package.py` rejects non-distribution builds. It creates and validates the native
runtime template and includes MSVC redistributable DLLs on Windows. The portable
payload is embedded as a ZIP so the freezer cannot rewrite engine binaries or
invalidate template hashes. Before publishing, the package is executed with an
empty `PATH` and `--verify-package`: it verifies SHA-256 hashes of the payload,
loads the bundled Tcl runtime and runs the bundled template validator.

The Linux workflow also extracts the final portable ZIP outside the checkout and
runs `--smoke-test` under Xvfb/software OpenGL with an empty `PATH`. The editor must
signal first-frame readiness within 30 seconds. The same workflow extracts
both native packages, checks that the installed launcher activates a trial
update, exports using only basic shell tools and launches each exported game.
