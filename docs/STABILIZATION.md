# Bubble Engine stabilization report

## UI polish — phase 1: project manager (2026-10-27)

| Item | Status and evidence | Change and regression coverage | Validation limits / review |
|---|---|---|---|
| Blue project-manager surfaces | CONFIRMED by the per-channel blue overrides in `apps/editor_app.cpp` | Removed those overrides; manager surfaces, cards, muted labels and previews now use classes in `commons/assets/themes/editor-dark.theme`, following the existing neutral dark palette. | Interactive layout and aesthetic review remain manual. Existing local entity/persistence/sample changes were preserved. |
| Square project preview corners | CONFIRMED by failing `GL3Test.ImageRoundsCornersAndPreservesTextureAndBorder` (corner pixels remained red; border was absent) | BGUI's image shader now respects radius and border, with a one-pixel antialiased contour. Previews and empty placeholders share a 6px radius. The regression reads actual OpenGL framebuffer pixels. | Test passed on the available Intel/Mesa display. Other GPU drivers and Windows remain untested. The test skips explicitly when no display/context is available. |

Validation: clean build of editor/runtime and all 39/39 CTest entries passed via `bash scripts/build.sh /tmp/cpp-bengine-ui-phase1-LDJeeW --test`. The pre-existing local persistence edit emits a missing-return warning in `get_repository_root`; that unrelated function was not changed in this phase.

## Project manager startup

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Project manager startup, Unity-inspired project selection, editor menu access, and new-project creation | Implemented and compiled; desktop interaction pending | [apps/editor_app.cpp](../apps/editor_app.cpp) shows recent projects with their latest scene previews, supports path and folder browsing, and opens a new project created by [apps/editor/systems/project_creation_system.cpp](../apps/editor/systems/project_creation_system.cpp). The editor's Files menu now opens the Project Manager with recent projects and a new-project dialog. | The creation flow validates the project name and location, creates standard asset/script folders and a starter project/scene, then loads it into the editor. `ProjectCreationTest.CreatesAnEmptyProjectWithStandardFolders` passes. Visual interaction still needs desktop validation. |

## Current build status

Validated with:

```
scripts/build.sh /tmp/cpp-bengine-project-manager-build --test
```

Result: clean configure and build succeeded; the current suite passed (`38/38` tests).

## Camera gizmos, preview, and viewport input

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Scene camera gizmos stay visible; runtime camera preview opens on hover | Implemented and compiled; desktop interaction pending | [apps/editor/tools/guizmo_system.hpp](../apps/editor/tools/guizmo_system.hpp) draws each scene camera frustum and a camera marker. [apps/editor/panels/scene_view.cpp](../apps/editor/panels/scene_view.cpp) shows the runtime camera framebuffer preview while its marker is hovered. | Runtime camera selection follows `runtime_app`: first scene camera in registry order, excluding the editor camera. The preview framebuffer is initialized at thumbnail size on demand. The old Show Frustum checkbox was removed because these gizmos are now always shown. |
| Viewport navigation, picking, and modal input ownership | Implemented and compiled; desktop interaction pending | [apps/editor/tools/camera_movement.hpp](../apps/editor/tools/camera_movement.hpp) confines navigation to the hovered viewport and uses right mouse drag; [apps/editor/tools/entity_picking_system.hpp](../apps/editor/tools/entity_picking_system.hpp) only queues picks inside the viewport and cancels them while a modal is open. | Left click remains available for selection and transform gizmos, avoiding camera-rotation/picking conflicts. No display-backed interaction test was available. |

Validated for this change with:

```
scripts/build.sh /tmp/cpp-bengine-camera-preview-build --test
```

Result: clean configure and build succeeded; all 38 tests passed. Interactive OpenGL behavior still needs desktop verification.

## BGUI menu bar and context-menu behavior

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Text-driven button/menu sizing | Confirmed and fixed | [external/cpp-bgui/core/source/elem/text.cpp](../external/cpp-bgui/core/source/elem/text.cpp) sized text from glyph advances and font line metrics, which can be smaller than the bitmap glyphs that FreeType actually draws. `MenuBarTest.TextContentSizeRoundsUpFractionalFontMetrics` reproduces both width (`24` vs required `25`) and height (`5` vs required `6`) under-sizing with fractional glyph metrics. | Layout now accounts for each glyph's bitmap bounds, offsets overhanging glyphs into the measured box, and uses the same measurement during drawing. |
| Menu-bar and context-option callbacks | Dispatch verified | [external/cpp-bgui/tests/menu_bar_tests.cpp](../external/cpp-bgui/tests/menu_bar_tests.cpp) synthesizes press/release events for a regular menu-bar button and a context-menu option; both callbacks run and the popup closes. | No separate callback-dispatch defect was reproduced. The editor was not manually exercised in a display-backed session. |

The regression tests use a synthetic font and BGUI's headless input path; they do not require a GPU or display.

## BGUI audit

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Current BGUI layout, element lifetime, input, and draw traversal | No additional defect confirmed | A clean CMake build compiled the BGUI library and test target. The full CTest run passed all 16 BGUI tests and all 20 engine tests (36/36 total). | Added [external/cpp-bgui/AGENTS.md](../external/cpp-bgui/AGENTS.md) with focused contribution and verification rules. Interactive visual behavior was not checked on a display/GPU in this environment. |

## Modal sizing and editor gizmo stacking

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Modal content sizing | Confirmed and fixed | [external/cpp-bgui/core/source/lay/layout.cpp](../external/cpp-bgui/core/source/lay/layout.cpp) now measures nested layouts even when their container is non-flex. `LinearTest.MeasuresContentInsideNonFlexLayouts` reproduces the previous zero-sized content. | This allows dynamically enabled modals to calculate their full content dimensions without waiting for a hover-style invalidation. |
| Gizmo drawn over active modals | Confirmed and fixed | [apps/editor/guizmo_system.hpp](../apps/editor/guizmo_system.hpp) suppresses grid/frustum/transform drawing while a root overlay modal is enabled. | The custom draw list is rendered after ordinary UI quads, so element-layer ordering alone cannot place a modal above it. Desktop visual verification remains pending. |

## Item-by-item status

| ID | Status | Evidence | Notes |
|---|---|---|---|
| P0-a | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) now writes a temporary file and validates the stream before replacing the target. | Prevents partial writes from destroying the previous project file. |
| P0-b | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) stages `staged_entity_names` and swaps it only after the full scene load succeeds. | Prevents losing names on a failed or partial load. |
| P0-c | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) now swaps `entities`, `mascaras`, and `live_entities` together. `ProjectPersistenceTest.KeepsLoadedEntityIdsInTheLiveRegistry` failed before the fix and passes after it. | The previous swap omitted `live_entities`, leaving loaded IDs inaccessible through `ecs::get()`. |
| P0-d | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) keeps the entity alive and its mask entry synchronized when `remove<T>()` removes its final component. `EcsInvariantTest.RemovingLastComponentKeepsEntityAlive` failed before the fix and checks both component removal and explicit entity removal. | Entity lifetime is independent of component count; `remove(id)` remains the operation that removes the entity and all its components. |
| P0-e | Confirmed and fixed | [commons/src/core/ecs.cpp](../commons/src/core/ecs.cpp) centralizes ID allocation and liveness through `live_entities`. | Avoids duplicate active IDs and destructive reuse. |
| P0-f | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) now guards the weak-ownership hookup and falls back safely if the ECS is not held by `std::shared_ptr`. | Avoids `std::bad_weak_ptr` during add operations. |
| P1-a | Confirmed and fixed | [README.md](../README.md) now matches the CMake build requirement by documenting C++20 support, while [CMakeLists.txt](../CMakeLists.txt) and [commons/CMakeLists.txt](../commons/CMakeLists.txt) remain configured with C++20. | This stale documentation mismatch has been corrected. |
| P1-b | Not confirmed | [CMakeLists.txt](../CMakeLists.txt) defines `BUILD_RUNTIME` and `BUILD_EDITOR`, and the workflow is now consistent with that option. | No dead option remains in the current root build contract. |
| P1-c | Confirmed and fixed | [tests/CMakeLists.txt](../tests/CMakeLists.txt) now declares the test dependency explicitly via FetchContent for GoogleTest. | This is required for clean builds without a system install. |
| P1-d | Confirmed | [commons/CMakeLists.txt](../commons/CMakeLists.txt) defines `COMMONS_ASSET_DIR` as a source-tree absolute path. | This is workable for development but not portable for installs or relocated runtimes. |
| P1-e | Partial: lifecycle hypothesis not confirmed; editor camera persistence gap confirmed and fixed | [apps/editor/systems/project_persistence_system.cpp](../apps/editor/systems/project_persistence_system.cpp) now saves the editor camera transform in project settings and restores it only after the project load succeeds. `ProjectPersistenceTest.KeepsEditorCameraOutOfSceneAndRoundTripsSceneCameras` covers position and rotation round-trip. | The earlier `m_transform` nulling claim remains unconfirmed. Camera framebuffer resources are intentionally not serialized. |
| P2-a | Partial | No extensible serializer registry is implemented at the component layer yet. | Design remains a follow-up proposal rather than a production change. |

## Project and scene file ownership

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Scene data stored separately from project settings | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) writes project settings plus a relative scene path to `.bengine`, while entities/components are written to a `bubble-engine-scene` `.scene` document. The FPP sample now uses [projects/fpp_demo/project.bengine](../projects/fpp_demo/project.bengine) and [projects/fpp_demo/main.scene](../projects/fpp_demo/main.scene). | Legacy combined `.bengine` documents remain readable; `.scene` files can also be opened directly. |
| “Renderizador ou transformação inválida” on loaded project entities | Confirmed and fixed | The new liveness regression test showed that scene entity IDs were present in the component maps but absent from `live_entities`; consequently `ecs::get()` returned null for those IDs, which reached the render system's invalid-component diagnostic. | The loader now stages and swaps the three ECS structures together. The regression test exercises loading IDs not previously present in the active registry without requiring OpenGL. |

## Test coverage added

- [tests/commons_tests.cpp](../tests/commons_tests.cpp): invariant checks for ECS liveness, final-component removal, mask consistency, invalid-ID behavior, and ID reuse.
- [tests/editor_persistence_tests.cpp](../tests/editor_persistence_tests.cpp): persistence round-trips, separated project/scene files, standalone and legacy scene loading, ECS liveness after loading, renderer materials/visibility, entity lock state, and project save/load stability checks.
- [tests/editor_persistence_tests.cpp](../tests/editor_persistence_tests.cpp): project creation generates the initial project/scene and standard asset/script folders.
- [external/cpp-bgui/tests/menu_bar_tests.cpp](../external/cpp-bgui/tests/menu_bar_tests.cpp): fractional text sizing before/after draw generation and menu/button callback dispatch.

## Renderer materials and entity editor state

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Renderer albedo materials are saved in scenes | Confirmed and covered | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) writes each renderer mesh's `material.albedo`; `ProjectPersistenceTest.SavesRendererMaterialVisibilityAndLockState` checks all four saved channels. | Material values remain optional on load for legacy scene files. |
| Renderer visibility and entity lock state survive project load | Confirmed and covered | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) writes and restores renderer `visible` and entity `locked`; `ProjectPersistenceTest.LoadsRendererVisibilityAndLockState` checks restored renderer and editor-context state. | Missing fields in older projects default to visible and unlocked. |

## Remaining risks / follow-up

- Pick a single official C++ standard for the project and align README + all build targets consistently.
- Decide whether the asset directory policy should be project-relative, executable-relative, install-relative, or configuration-driven.
- Consider a component serializer registry if extensibility becomes a priority beyond this stabilization pass.
- Full GPU/display validation for the editor and FPP example remains pending in a desktop environment with a working display stack.

## Cross-platform build tooling

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Select Linux/Windows target, choose an output directory, download toolchain, and build in the editor | Implemented; Linux-host Windows build validated, Linux-target and WSL2 validation pending | [apps/editor/panels/editor_ui.cpp](../apps/editor/panels/editor_ui.cpp) provides Linux/Windows actions, platform icons, progress, and a browsable output-directory selector. [scripts/platform_build.sh](../scripts/platform_build.sh) downloads SHA-256-pinned Zig and PowerShell host tools, bootstraps MinGW `windres` on Debian/Ubuntu hosts, and prepares target libraries through vcpkg; [scripts/platform_build.ps1](../scripts/platform_build.ps1) routes Windows-host builds through WSL2. | Native editor/test build and CTest passed (34/34). `scripts/platform_build.sh windows ...` produced a PE32+ x64 `runtime_app.exe` from Linux. The Linux-target dependency/build validation is in progress; WSL2 execution is unavailable on this host. |
| Build scripts and CI cover native Windows and Linux | Linux build and tests passed; Windows pending CI | [scripts/build.sh](../scripts/build.sh), [scripts/build.ps1](../scripts/build.ps1), and [.github/workflows/build_and_test.yml](../.github/workflows/build_and_test.yml) provide native builds and CI matrix coverage. | `ctest --test-dir build --output-on-failure` passed all 34 tests. Windows cannot be executed in this environment. |

## Scene panel removal and window controls

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Removing the Scene panel left viewport systems holding pointers to its destroyed image | Confirmed by ownership/lifetime path and fixed | [apps/editor/panels/scene_view.cpp](../apps/editor/panels/scene_view.cpp) now owns viewport sync, camera movement, and picking updates in a UI element under the Scene window. The surface clears the shared image handle during destruction; the asset-drop callback checks whether the viewport remains open. | The application was compiled, but closing the panel was not manually exercised in a desktop session. |
| Window traffic lights were vertically offset | Confirmed by geometry regression test and fixed | [commons/assets/themes/editor-dark.theme](../commons/assets/themes/editor-dark.theme) centers `window-controls` and removes inherited padding. `EditorThemeTest.VerticallyCentersWindowTrafficLights` measures all three controls against the header center without requiring OpenGL. | Headless geometry test passes. |

## Undocked window content drift

| Check | Status | Evidence | Changes and limits |
|---|---|---|---|
| Content shifts downward repeatedly after undocking a padded window | CONFIRMED and fixed | `DockTest.UndockedContentFollowsWindowMovement` failed before the fix: a title drag of 60 pixels moved the nested button by 68 pixels. The test uses the editor's 4-pixel top/bottom header padding and the mutation/arrange/commit geometry cycle. | `external/cpp-bgui/core/source/elem/window.cpp` sizes the icon from the header content height, excluding vertical padding and icon margins. Previously, the icon fed the padding back into `wrap_content`, growing the header every frame. Public API unchanged. |
| Regression coverage | Added | `external/cpp-bgui/tests/dock_tests.cpp` simulates undocking and title dragging, checks that nested content follows the window and remains inside it, and verifies stable geometry across subsequent frames. | Headless geometry coverage; manual editor dragging, resizing, and visual rendering remain unvalidated because no interactive desktop verification was performed. No design decisions pending; other overflow causes are outside this confirmed reproduction. |

Validation for this change (2026-10-27):

- Clean rebuild attempted with `cmake --build out-build-check --clean-first --parallel 2`, using the existing CMake configuration with editor, runtime, and tests enabled. `editor_app`, `runtime_app`, and BGUI `all_tests` compiled successfully. The full build failed linking `editor_render_tests` due to undefined BGUI symbols (`detail::log_out`, `detail::log_err`, `get_context_size`, `get_projection`); this target and its CMake configuration contain existing local work outside this change and were left untouched.
- `ctest --test-dir out-build-check -R '^DockTests$' --output-on-failure` passed; all 18 docking regressions pass, including the new test after the clean rebuild.
- Full CTest: 41/43 entries passed. `ProjectPersistenceTest.SavesRendererMaterialVisibilityAndLockState` failed at `tests/editor_persistence_tests.cpp:364` because serialized PBR data lacked `metallic`; `editor_render_tests_NOT_BUILT` could not run after the link failure. Neither issue was changed as part of the window fix. Consequently, a successful full build/test run remains pending.

Validation: configured a fresh `/tmp/cpp-bengine-native-dialog` build using the existing local GoogleTest source, then ran `bash scripts/build.sh /tmp/cpp-bengine-native-dialog --test`. Editor and runtime compiled. The complete build was blocked by unresolved BGUI symbols in the pre-existing local `editor_render_tests` target. Running CTest separately yielded 46 passed, one failed (`ProjectPersistenceTest.SavesRendererMaterialVisibilityAndLockState`: missing `pbr.metallic`) and one not run (`editor_render_tests_NOT_BUILT`), out of 48 entries. Those persistence/render test files were already locally modified before this change and were preserved. The five new native-dialog tests passed. Full green validation remains blocked by those unrelated failures; no stabilization diagnostic fixes were attempted in this phase.
