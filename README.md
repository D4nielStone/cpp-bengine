Bubble Engine
===

![preview](image.png)

Bubble Engine is an experimental C++ engine for windowing, rendering,
physics, and component-based entities. The project is still under
development and includes a minimal example in examples/cube_example.cpp.
Dependencies
The following must be installed:
 * CMake 3.20 or higher
 * A compiler with C++23 support
 * Lua 5.3
 * GLFW
 * GLM
 * Assimp
 * FreeImage
 * FreeType
 * Bullet
The sol2, rapidjson, and glad libraries are maintained in the repository or
as submodules. When cloning the project, initialize them with:
git submodule update --init --recursive

Building
From the project root:
```
cmake -S . -B out
cmake --build out
```
The example executables will be generated in the out/examples directory. To
run the main example:
./out/examples/scene1

Structure
 * commons/: reusable static library of the engine.
 * examples/: small programs utilizing the library.
 * external/: dependencies included in the project.
Installing
To install the library and headers to a specific prefix:
```
cmake --install out --prefix install
```
