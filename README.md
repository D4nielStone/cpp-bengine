Bubble Engine
===

![PREVIEW](image.jpg)

# About

An experimental C++ engine for windowing, rendering, physics, and component-based entities. The project is still under development and provides both runtime and editor applications.

# GUI

The engine uses Bubble GUI to provide a simple and easy-to-use interface for the editor application. The GUI supports native docking and windowing.

The engine bundles Hack Nerd Font for consistent, antialiased interface text.
BGUI's bundled 3270 Nerd Font has an intentionally pixelated terminal style, so
it remains dedicated to icon glyphs.

The engine also uses the BGUI API to build its own transform gizmos.

The editor's **Files** menu saves and loads the dock layout through BGUI in
`editor-ui.cfg`. Project settings and the main-scene reference live in
`project.bengine`; entity/component data lives in the referenced `.scene` file.
The **Edit** menu and `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`) undo and redo
scene edits, including Inspector changes, entity and component creation or
removal, and transform-gizmo drags. Loading another project clears this history.
Scene files support transforms, renderers, cameras, directional lights,
physics bodies/colliders, and Lua code components; saving fails with an error if
an entity contains a component without project serialization support. The
editor's Inspector panel is docked on the right and lets you edit transform,
camera, and directional-light properties, as well as add or remove supported
components. Selecting an entity in either the scene or the entity list updates
the Inspector.

The **Arquivos** panel shows the project folders on a dark sidebar and the
selected folder's files beside it. Selecting a `.world`, `.bengine`, or `.scene`
file reveals the option to make it the main scene; this choice is stored in
`.bengine-main-scene` at the project root. Drag supported 3D model files from
the panel into the Scene viewport to add them as entities.

Use the editor's centered play icon to open the current scene in a separate
runtime window. The icon sits in a dark toolbar container. The runtime window
displays the game camera framebuffer, along with frame time, FPS, CPU render
time, and draw counts. Press the stop icon in the editor to close the runtime
window. The **Editor Diagnostics** panel continues to show metrics for the
editor viewport.

# Dependencies

The following dependencies must be installed:

- CMake 3.20 or higher

- A compiler with C++20 support

- Lua 5.3

- GLFW

- GLM

- Assimp

- FreeImage

- FreeType

- Bullet

> The sol2, rapidjson, and glad libraries are maintained in the repository or included as submodules. 

When cloning the project, initialize them with:
```
git submodule update --init --recursive
```

# Building

From the project root, build both executables on Linux:
```
./scripts/build.sh
```

On Windows, run the PowerShell build helper:
```powershell
.\scripts\build.ps1
```

Both helpers configure and build the editor and runtime in `build/` by default.
Pass a build directory to use another location. Add `--test` on Linux or
`-RunTests` on Windows to also build and run the test suite.

Once the editor is open, select **Build > Build Runtime** to open the platform
build modal. Choose **Linux x64** or **Windows x64**; the first use downloads
the Zig cross-compiler when needed and prepares target libraries through
vcpkg. The modal displays download/build progress and the result. Detailed
output is saved as `editor-build-linux.log` or `editor-build-windows.log` in
the selected build directory. Enter a path directly or use **Browse** to pick
an existing folder; artifacts are placed under
`<directory>/platform-builds/<target>/apps`. Stop Play mode before building.

Linux builds use the host's Linux toolchain and require CMake, a C++ compiler,
pkg-config, and the OpenGL/GLFW/X11 development packages used by the engine.
Building from Windows uses the default WSL2 distribution for both targets;
install a WSL2 distribution first. On Ubuntu/WSL2, install Linux target
requirements with:
```sh
sudo apt-get update
sudo apt-get install -y cmake build-essential pkg-config libgl1-mesa-dev \
  libglfw3-dev xorg-dev libxinerama-dev libxcursor-dev libxrandr-dev libxi-dev \
  libglu1-mesa-dev libassimp-dev libbullet-dev libfreetype6-dev \
  liblua5.3-dev libpng-dev
```

The platform build downloads pinned Zig and PowerShell host tools as needed,
plus a pinned vcpkg release into the user cache to prepare target dependencies.
Windows cross-builds on Linux also download the MinGW-w64 resource compiler
with `apt-get download` (no root access); Debian/Ubuntu-based hosts need
`apt-get` and `dpkg-deb`. On other Linux distributions, install the
`x86_64-w64-mingw32-windres` tool from the distribution's MinGW-w64 binutils
package and ensure it is on `PATH`. Set `VCPKG_ROOT` to an existing vcpkg
checkout to reuse it instead.

The GitHub Actions workflow builds and tests natively on Linux and Windows and
publishes the editor and runtime executables as per-platform workflow
artifacts. Platform builds from the editor generate the runtime under
`build/platform-builds/<target>/apps` (or the selected build directory's
corresponding folder). Run the executable with that `apps` folder as the
working directory so its bundled assets resolve correctly.

Run either application with:

```
./out/apps/runtime_app
./out/apps/editor_app
```

To open a scene directly in the editor, pass its file path with `--project`:

```
./out/apps/editor_app --project game.world
```

The editor's **Load Project** and **Save Project** menu actions use the same
path supplied at startup. Without `--project`, they continue to use
`project.bengine`. The **Load Project** action now opens a modal that accepts
a project directory or a project file path, and it resolves common folders such
as `project.bengine`, `game.world`, or a project directory containing one of
those files. Older `.bengine` files that still embed entities remain loadable.

A ready-to-use FPP sample project is available in `projects/fpp_demo/` and uses
entities, physics, collision, and Lua scripts for gameplay logic.
Open it with `./build/apps/editor_app --project projects/fpp_demo/project.bengine`
or select its directory through the editor's **Load Project** folder browser.
In runtime, the player controller supports WASD movement and mouse look.

# Structure

> commons/: Reusable static library containing the engine's core functionality.

> apps/: Runtime and editor applications that use the engine.

> external/: Third-party dependencies included in the project.

# Installing

To install the library and headers to a specific prefix, run:

```
cmake --install out --prefix install
```