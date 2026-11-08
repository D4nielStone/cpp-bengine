Bubble Engine — Agent Instructions

Project Overview

Bubble Engine is a modular C++20 game engine with a native editor.

Main technologies:

- C++20 and CMake
- OpenGL and GLFW
- BGUI for the editor interface
- ECS for entities and components
- Bullet Physics
- Lua scripting
- Assimp for 3D assets

The current development objective is to stabilize the version declared in the root CMakeLists.txt.

Prioritize reliability, maintainability, correctness and backward compatibility over new features.

Repository Structure

- "commons/": reusable engine components, rendering, ECS, resources and runtime functionality.
- "apps/editor/": native editor, panels, tools and editor systems.
- "apps/project/": project and scene persistence shared by the editor and standalone runtime.
- "apps/": editor, standalone runtime and optional local MCP server.
- "apps/editor/core/" and "apps/editor/services/": editor state, commands and services.
- "apps/mcp/": local MCP tools; keep protocol handling separate from UI and engine state.
- "docs/": feature guides, API contracts and stabilization evidence.
- "external/cpp-bgui/": maintained BGUI submodule; follow its own AGENTS.md for changes.
- "tests/": automated tests.
- The editor's project creation system provides empty and FPP templates; no sample game assets are bundled.

Inspect the actual repository before assuming any API, class, function or file exists.

Architecture Rules

1. Preserve the existing modular architecture.
2. Do not introduce duplicate systems, managers or utilities.
3. Search for existing implementations before adding new abstractions.
4. Prefer small, localized changes over large rewrites.
5. Preserve public APIs unless changes are explicitly authorized.
6. Keep editor-specific logic separate from reusable engine functionality.
7. Avoid unnecessary dependencies between modules.
8. Respect existing namespaces and naming conventions.
9. Do not replace established systems with unrelated architectures.
10. Do not modify third-party dependencies unless absolutely necessary. BGUI is a maintained submodule: requested BGUI work belongs there, without modifying unrelated dependencies.
11. Inspect git status in both the engine and affected submodules before editing. Preserve existing uncommitted work and do not reset or stage unrelated changes.

C++ Requirements

- Use C++20.
- Follow existing ownership and lifetime conventions.
- Prefer RAII for resource management.
- Avoid raw owning pointers.
- Prevent dangling references and use-after-free.
- Validate object and resource lifetimes.
- Avoid unnecessary allocations in per-frame operations.
- Do not introduce global mutable state without justification.
- Keep headers focused and avoid unnecessary includes.
- Avoid unnecessary comments and generated boilerplate.

Use the namespace declared by the relevant module. Preserve existing "commons", "bgui", "editor::ui" and "Bubble" namespaces wherever applicable.

ECS Requirements

- Respect the existing entity and component lifecycle.
- Never retain references to components without verifying lifetime guarantees.
- Handle entity deletion and component removal safely.
- Avoid invalidating active iterators during system updates.
- Do not duplicate component state without a synchronization strategy.
- Property animations use commons::animation components referencing reusable .banim animation_clip assets, with playback in animation_system. Keep curves out of scene component records. Expose custom channels through component's optional animation-property methods; validate setters and use stable property names. Never implement playback by reloading serialized components/assets each frame.
- The animation editor records property changes automatically while Record is enabled; do not add prospective property/channel pickers or manual capture workflows. Keep previews and undo/redo out of automatic recording.
- Retain dirty animation assets after entity deletion or reassignment, and detect conflicting disk edits before overwriting files.
- Preserve animation tracks during save/load and entity copy/paste. Resolve live targets on each sample and verify component identity for undo/redo.
- Preserve serialization compatibility whenever possible.

Rendering Requirements

- Respect OpenGL context ownership.
- Never access GPU resources after their context is destroyed.
- Ensure framebuffers and textures have valid dimensions.
- Handle viewport resizing safely.
- Restore relevant OpenGL state after rendering operations.
- Avoid unnecessary framebuffer recreation.
- Verify that rendering changes do not break editor overlays or gizmos.

BGUI and Editor Requirements

- Reuse existing BGUI widgets and layout systems.
- Prefer bgui::panel_facade (panel_facade.hpp) for new custom dock panels and bgui::panel_content for composing existing layouts. Use native() and add<T>() for specialized widgets instead of adding a second UI system.
- The facade borrows widgets; the BGUI tree owns them. Do not retain facade views or widget references after removing their window or replacing the root layout.
- Keep editor_panel/panel_manager responsible for editor lifecycle and editor commands/history responsible for edits. The BGUI facade must remain independent of ECS, project state and undo.
- See docs/EDITOR_PANELS.md and external/cpp-bgui/docs/PANEL_FACADE.md for extension examples.
- Preserve docking, floating windows and panel modularity.
- Avoid hardcoded screen coordinates when layout information is available.
- Prevent intermediate inconsistent layout states.
- Keep input handling consistent across viewport, gizmos and panels.
- Avoid duplicating UI input processing.
- Respect widget ownership and destruction rules.
- Do not add dependencies on unimplemented BGUI APIs.

Project Persistence

- Preserve project and scene data integrity.
- Validate file paths and serialized data.
- Prefer atomic or recoverable save operations.
- Handle malformed and missing files gracefully.
- Maintain compatibility with existing project formats when possible.
- Add regression tests for serialization changes.
- Never silently discard user data.
- Startup resumes the last successfully opened available project. Keep the menubar Project Manager connected to the main manager and preserve unsaved-change guards and workspace saving. Never nest the native window loop to show it.

Lua API

- Use the current project-loading architecture.
- Inspect existing Lua bindings before adding new ones.
- Every API change (addition, removal, signature, type, default, lifetime or behavior) must update docs/LUA_API.md, the relevant docs/lua-api guides and examples, the in-editor API reference/completion, visual scripting command catalog and argument fields, and affected Empty/FPP templates in the same change. Add regression coverage for the binding and visual representation; document intentional compatibility limits. Never expose visual commands for APIs absent from the runtime, or setters for read-only properties.
- Maintain consistency with the generated Empty/FPP templates and docs/lua-api examples; inspect the current templates rather than assuming bundled sample assets exist.
- Validate entity and component references exposed to Lua.
- Report script errors without crashing the editor.
- Preserve existing scripts unless an API migration is explicitly requested.

Build System

- Keep CMake targets modular.
- Do not add dependencies without justification.
- Do not hardcode developer-specific absolute paths.
- Preserve supported toolchains.
- Validate build changes with a clean configuration.
- Keep editor and runtime builds reproducible.
- Use scripts/build.sh <build-dir> --test for the integrated build and CTest suite. Check current CMake options before using reduced builds; BGUI also supports standalone headless tests.
- For BGUI changes made here, record evidence and validation limits in docs/STABILIZATION.md.

Testing and Validation

For every behavioral change:

1. Identify the affected modules.
2. Reproduce the original issue when possible.
3. Implement the smallest correct fix.
4. Add or update relevant regression tests.
5. Build affected targets.
6. Run relevant CTest tests.
7. Check for warnings and resource-lifetime problems.
8. Report what was actually tested.

Never claim a build or test passed unless it was executed successfully.

If validation cannot run, explain the exact limitation.

Stability Priorities

Prioritize work in this order:

1. Build and dependency failures.
2. Crashes, memory corruption and invalid resource lifetimes.
3. Project persistence and data loss.
4. Editor/runtime lifecycle failures.
5. Rendering and viewport correctness.
6. ECS, physics and scripting correctness.
7. UI consistency and usability.
8. Performance optimization.
9. New features.

Do not introduce unrelated features while fixing stability issues.

Required Workflow

Before editing:

- Inspect relevant files and dependencies.
- Identify the root cause.
- Describe the intended changes.
- Consider compatibility risks.

During implementation:

- Make focused changes.
- Preserve existing architecture.
- Avoid unrelated refactoring.
- Add tests where appropriate.

After implementation:

- Review the complete diff.
- Build and run relevant tests when possible.
- Check for regressions.
- Summarize changed files.
- Report remaining risks and unverified behavior.

Completion Criteria

A task is complete only when:

- The requested behavior is implemented.
- Relevant interfaces remain compatible or migrations are documented.
- No known regression was introduced.
- Relevant validation has been performed or limitations disclosed.
- The result is maintainable and consistent with the engine architecture.
