# Bubble Engine stabilization report

## Current build status

Validated with:

```
cmake -S . -B build-menu-check -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build build-menu-check --parallel 2
ctest --test-dir build-menu-check --output-on-failure
```

Result: clean configure and build succeeded; the current suite passed (`29/29` tests).

## BGUI menu bar and context-menu behavior

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Text-driven button/menu sizing | Confirmed and fixed | [external/cpp-bgui/core/source/elem/text.cpp](../external/cpp-bgui/core/source/elem/text.cpp) sized text from glyph advances and font line metrics, which can be smaller than the bitmap glyphs that FreeType actually draws. `MenuBarTest.TextContentSizeRoundsUpFractionalFontMetrics` reproduces both width (`24` vs required `25`) and height (`5` vs required `6`) under-sizing with fractional glyph metrics. | Layout now accounts for each glyph's bitmap bounds, offsets overhanging glyphs into the measured box, and uses the same measurement during drawing. |
| Menu-bar and context-option callbacks | Dispatch verified | [external/cpp-bgui/tests/menu_bar_tests.cpp](../external/cpp-bgui/tests/menu_bar_tests.cpp) synthesizes press/release events for a regular menu-bar button and a context-menu option; both callbacks run and the popup closes. | No separate callback-dispatch defect was reproduced. The editor was not manually exercised in a display-backed session. |

The regression tests use a synthetic font and BGUI's headless input path; they do not require a GPU or display.

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
| P0-d | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) and [commons/src/core/ecs.cpp](../commons/src/core/ecs.cpp) keep `live_entities`, `mascaras`, and `entities` aligned, and `validate_invariants()` checks consistency. | Prevents stale masks after component removal. |
| P0-e | Confirmed and fixed | [commons/src/core/ecs.cpp](../commons/src/core/ecs.cpp) centralizes ID allocation and liveness through `live_entities`. | Avoids duplicate active IDs and destructive reuse. |
| P0-f | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) now guards the weak-ownership hookup and falls back safely if the ECS is not held by `std::shared_ptr`. | Avoids `std::bad_weak_ptr` during add operations. |
| P1-a | Confirmed | [README.md](../README.md) still says “compiler with C++23 support”, while [CMakeLists.txt](../CMakeLists.txt) and [commons/CMakeLists.txt](../commons/CMakeLists.txt) are still configured with C++20. | This mismatch remains a documentation/build-policy decision. |
| P1-b | Not confirmed | [CMakeLists.txt](../CMakeLists.txt) defines `BUILD_RUNTIME` and `BUILD_EDITOR`, and the workflow is now consistent with that option. | No dead option remains in the current root build contract. |
| P1-c | Confirmed and fixed | [tests/CMakeLists.txt](../tests/CMakeLists.txt) now declares the test dependency explicitly via FetchContent for GoogleTest. | This is required for clean builds without a system install. |
| P1-d | Confirmed | [commons/CMakeLists.txt](../commons/CMakeLists.txt) defines `COMMONS_ASSET_DIR` as a source-tree absolute path. | This is workable for development but not portable for installs or relocated runtimes. |
| P1-e | Not confirmed | There is no current nulling of `m_transform` in the editor/runtime codebase search; the runtime camera lifecycle is not currently reproducing the issue. | No direct evidence of the risk remains in the checked sources. |
| P2-a | Partial | No extensible serializer registry is implemented at the component layer yet. | Design remains a follow-up proposal rather than a production change. |

## Project and scene file ownership

| Check | Status | Evidence | Notes |
|---|---|---|---|
| Scene data stored separately from project settings | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) writes project settings plus a relative scene path to `.bengine`, while entities/components are written to a `bubble-engine-scene` `.scene` document. The FPP sample now uses [projects/fpp_demo/project.bengine](../projects/fpp_demo/project.bengine) and [projects/fpp_demo/main.scene](../projects/fpp_demo/main.scene). | Legacy combined `.bengine` documents remain readable; `.scene` files can also be opened directly. |
| “Renderizador ou transformação inválida” on loaded project entities | Confirmed and fixed | The new liveness regression test showed that scene entity IDs were present in the component maps but absent from `live_entities`; consequently `ecs::get()` returned null for those IDs, which reached the render system's invalid-component diagnostic. | The loader now stages and swaps the three ECS structures together. The regression test exercises loading IDs not previously present in the active registry without requiring OpenGL. |

## Test coverage added

- [tests/commons_tests.cpp](../tests/commons_tests.cpp): invariant checks for ECS liveness, mask consistency, invalid-ID behavior, and ID reuse.
- [tests/editor_persistence_tests.cpp](../tests/editor_persistence_tests.cpp): persistence round-trips, separated project/scene files, standalone and legacy scene loading, ECS liveness after loading, renderer materials/visibility, entity lock state, and project save/load stability checks.
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
