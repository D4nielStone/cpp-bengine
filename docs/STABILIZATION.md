# Bubble Engine stabilization report

## Current build status

Validated with:

```
cmake -S . -B build-check -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build build-check --parallel 2
ctest --test-dir build-check --output-on-failure
```

Result: build succeeded and the current suite passed (`28/28` tests). The repository is therefore in a clean, passing state for the tests available in this environment.

## Item-by-item status

| ID | Status | Evidence | Notes |
|---|---|---|---|
| P0-a | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) now writes a temporary file and validates the stream before replacing the target. | Prevents partial writes from destroying the previous project file. |
| P0-b | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) stages `staged_entity_names` and swaps it only after the full scene load succeeds. | Prevents losing names on a failed or partial load. |
| P0-c | Confirmed and fixed | [apps/editor/project_persistence_system.cpp](../apps/editor/project_persistence_system.cpp) loads into a fresh `staged_registry` and only swaps `m_registry->entities` / `mascaras` after validation. | Avoids partially mutating the active ECS. |
| P0-d | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) and [commons/src/core/ecs.cpp](../commons/src/core/ecs.cpp) keep `live_entities`, `mascaras`, and `entities` aligned, and `validate_invariants()` checks consistency. | Prevents stale masks after component removal. |
| P0-e | Confirmed and fixed | [commons/src/core/ecs.cpp](../commons/src/core/ecs.cpp) centralizes ID allocation and liveness through `live_entities`. | Avoids duplicate active IDs and destructive reuse. |
| P0-f | Confirmed and fixed | [commons/include/core/ecs.hpp](../commons/include/core/ecs.hpp) now guards the weak-ownership hookup and falls back safely if the ECS is not held by `std::shared_ptr`. | Avoids `std::bad_weak_ptr` during add operations. |
| P1-a | Confirmed | [README.md](../README.md) still says “compiler with C++23 support”, while [CMakeLists.txt](../CMakeLists.txt) and [commons/CMakeLists.txt](../commons/CMakeLists.txt) are still configured with C++20. | This mismatch remains a documentation/build-policy decision. |
| P1-b | Not confirmed | [CMakeLists.txt](../CMakeLists.txt) defines `BUILD_RUNTIME` and `BUILD_EDITOR`, and the workflow is now consistent with that option. | No dead option remains in the current root build contract. |
| P1-c | Confirmed and fixed | [tests/CMakeLists.txt](../tests/CMakeLists.txt) now declares the test dependency explicitly via FetchContent for GoogleTest. | This is required for clean builds without a system install. |
| P1-d | Confirmed | [commons/CMakeLists.txt](../commons/CMakeLists.txt) defines `COMMONS_ASSET_DIR` as a source-tree absolute path. | This is workable for development but not portable for installs or relocated runtimes. |
| P1-e | Not confirmed | There is no current nulling of `m_transform` in the editor/runtime codebase search; the runtime camera lifecycle is not currently reproducing the issue. | No direct evidence of the risk remains in the checked sources. |
| P2-a | Partial | No extensible serializer registry is implemented at the component layer yet. | Design remains a follow-up proposal rather than a production change. |

## Test coverage added

- [tests/commons_tests.cpp](../tests/commons_tests.cpp): invariant checks for ECS liveness, mask consistency, invalid-ID behavior, and ID reuse.
- [tests/editor_persistence_tests.cpp](../tests/editor_persistence_tests.cpp): persistence round-trips, invalid JSON, duplicate IDs, and project save/load stability checks.

## Remaining risks / follow-up

- Pick a single official C++ standard for the project and align README + all build targets consistently.
- Decide whether the asset directory policy should be project-relative, executable-relative, install-relative, or configuration-driven.
- Consider a component serializer registry if extensibility becomes a priority beyond this stabilization pass.
- Full GPU/display validation for the editor and FPP example remains pending in a desktop environment with a working display stack.
