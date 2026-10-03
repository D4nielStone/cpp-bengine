Bubble Engine
===

<video controls src="gif.gif" title="Bubble Engine"></video>

# About

An experimental C++ engine for windowing, rendering, physics, and component-based entities. The project is still under development and provides both runtime and editor applications.

# GUI

The engine uses Bubble GUI to provide a simple and easy-to-use interface for the editor application. The GUI supports native docking and windowing.

The engine also uses the BGUI API to build its own transform gizmos.

# Dependencies

The following dependencies must be installed:

- CMake 3.20 or higher

- A compiler with C++23 support

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

From the project root, run:
```
cmake -S . -B out
cmake --build out
```

The runtime and editor applications will be generated in the out/apps directory.

Run either application with:

```
./out/apps/runtime_app
./out/apps/editor_app
```

# Structure

> commons/: Reusable static library containing the engine's core functionality.

> apps/: Runtime and editor applications that use the engine.

> external/: Third-party dependencies included in the project.

# Installing

To install the library and headers to a specific prefix, run:

```
cmake --install out --prefix install
```