# Bubble Engine stabilization report

## Avaliação de prontidão — 2026-10-28

Avaliação e plano completos em [STABILITY_ASSESSMENT.md](STABILITY_ASSESSMENT.md).
Estimativa de engenharia: **61/100 de prontidão** (faixa de julgamento 55–70),
classificação **alpha funcional**, e **21–34 dias úteis** para o escopo proposto
de primeira versão Linux/Windows. A pontuação é ponderada por risco/evidência,
não porcentagem de código concluído. Nenhuma correção de produção nesta etapa.

| Item | Status / evidência | Próxima ação e limites |
|---|---|---|
| A1 — ownership ECS/componentes | CONFIRMADO: teste dirigido guarda `weak_ptr`, cria entidade e sai do escopo; registro não expira. Repetido contra build limpo. | Corrigir ciclo de ownership e testar desligamento; API pública e liberação com contexto OpenGL exigem decisão explícita. Não foi medido crescimento em sessão prolongada. |
| A2 — verificação de invariantes | CONFIRMADO: máscara declara Camera ausente e `validate_invariants()` retorna true. | Adicionar regressão e comparar máscara/componentes. O teste injeta corrupção nos mapas públicos; não demonstra corrupção pelo fluxo de UI. |
| A3 — opções de build | CONFIRMADO: editor/runtime continuam como alvos com ambas as opções OFF. | Corrige a conclusão histórica excessiva sobre P1-b: as opções existem, mas não governam `apps/CMakeLists.txt`. |
| A4 — distribuição | PARCIAL: modo relativo e cópia de assets existem na exportação; CI nativa publica só executáveis. | Validar pacote relocável com dependências fora do checkout nas duas plataformas. |
| A5 — retorno ausente | PARCIAL: compilador avisa sobre `get_repository_root`; chamada problemática não reproduzida. | Confirmar contrato antes da correção. |
| A6 — configuração Release Linux | CONFIRMADO no cache: helper usa `--config Release`, mas não define tipo de build em Makefiles. | Configurar e testar explicitamente Release otimizado. |

Validação: `bash scripts/build.sh /tmp/bengine-stability-audit-20261028 --test`;
build novo de editor/runtime e **111/111 entradas CTest passaram**, sem skips
nos logs detalhados, inclusive verificações OpenGL. Dois testes extras de
diagnóstico em `/tmp/bengine-audit-probes.cpp` falharam como esperado e foram
repetidos contra as bibliotecas novas; não foram adicionados à suíte normal.
Logs: `/tmp/bengine-stability-audit-20261028.log`,
`/tmp/bengine-audit-probes-clean.log`. O host selecionou Lua 5.4, sem validar
Lua 5.3. Windows, sanitizadores, pacote relocável, falha real de disco/energia
e uso interativo completo continuam sem validação nesta avaliação. Alterações
locais do FPP foram preservadas. Estratégia: ciclo de vida → build/pacote →
aceitação → contratos/persistência → candidata de release, uma fase por vez.

## Inspector layout — compact properties (2026-10-28)

| Item | Status and evidence | Change and regression coverage | Validation limits / review |
|---|---|---|---|
| Compact, aligned property controls | CONFIRMED: `EditorInteractionTest.InspectorUsesCompactAlignedPropertyRows` initially failed because numeric inputs were 15px high instead of the shared 22px target; code also placed vector names and controls on separate lines. | Vector properties now occupy one row. Numeric, model and material-map properties share a 112px label column, 22px inputs and 2px vertical row margins; component padding is reduced to 4px vertically. The new test checks vector row structure, numeric input alignment and height. Existing material edit/Undo coverage passes. | No public API or component behavior changed. Final aesthetic review, narrow-panel usability, Windows and display-scale variations remain unvalidated. Existing FPP project edits are preserved. |

Validation: clean editor/runtime build succeeded and all 111/111 CTest entries passed in `/tmp/cpp-bengine-inspector-layout-build`, using `bash scripts/build.sh /tmp/cpp-bengine-inspector-layout-build --test`. Both directed Inspector tests passed in the existing local build. Log: `/tmp/cpp-bengine-inspector-layout-validation.log`.

## UI polish — phase 1: project manager (2026-10-27)

| Item | Status and evidence | Change and regression coverage | Validation limits / review |
|---|---|---|---|
| Blue project-manager surfaces | CONFIRMED by the per-channel blue overrides in `apps/editor_app.cpp` | Removed those overrides; manager surfaces, cards, muted labels and previews now use classes in `commons/assets/themes/editor-dark.theme`, following the existing neutral dark palette. | Interactive layout and aesthetic review remain manual. Existing local entity/persistence/sample changes were preserved. |
| Square project preview corners | CONFIRMED by failing `GL3Test.ImageRoundsCornersAndPreservesTextureAndBorder` (corner pixels remained red; border was absent) | BGUI's image shader now respects radius and border, with a one-pixel antialiased contour. Previews and empty placeholders share a 6px radius. The regression reads actual OpenGL framebuffer pixels. | Test passed on the available Intel/Mesa display. Other GPU drivers and Windows remain untested. The test skips explicitly when no display/context is available. |

Validation: clean build of editor/runtime and all 39/39 CTest entries passed via `bash scripts/build.sh /tmp/cpp-bengine-ui-phase1-LDJeeW --test`. The pre-existing `get_repository_root` stub emits a missing-return warning; that unrelated function was not changed in this phase.

## UI polish — phase 2: viewport, hierarchy and Inspector

| Item | Status and evidence | Change and tests | Validation limits / review |
|---|---|---|---|
| Dragging the cursor to navigate | CONFIRMED: `EditorInteractionTest.RightDragLooksAroundWithoutMovingTheEye` failed because right drag did not rotate the camera | Right drag or Space + primary drag looks around from a fixed eye; Shift + Space + primary drag pans. Space + WASD/QE works without holding a mouse button. Trackpad scrolling dollies, Shift + scroll pans vertically, and horizontal scroll pans sideways. Position and rotation use exponential lerp toward independent target poses, including after release. Stale cursor deltas and restored poses are handled. A viewport hint lists the controls. | Tests use an injected clock to check smoothing, convergence, fixed-eye rotation, primary-drag pan and keyboard movement. Subjective feel and physical trackpad gestures still require manual review. |
| Hierarchy disclosure and deletion | CONFIRMED: rows used ordinary `v`/`>` buttons, and the remove button hid its label without supplying an icon | Rows now use BGUI tree selection, separate transparent chevrons, leaf handling and drag callbacks. Expansion survives refresh. A trash icon and Delete key remove the selected entity; text focus, modals, locks and the editor camera are protected. Tree and editor interaction tests cover selection/disclosure/drop independence and deletion/undo, including text-focus protection. | Existing row-wide reparent hit targets were retained. Manual drag/drop and appearance review remain pending. |
| Component controls | CONFIRMED: the add list contained only Camera and Directional Light; component removal occupied a full content row | The add list also offers missing Transform, Renderer and Physics components. Removal uses a compact header action. Model assignment accepts a path or a drop; changes and removals support undo. Inspector structure and values refresh after undo/redo. Empty renderers can be saved/reloaded before a model is assigned. | Script attachment is the next phase. The existing one-component-per-type ECS API is preserved. |
| PBR editing and persistence | CONFIRMED: Inspector exposed only albedo; failing save and OpenGL round-trip tests lost metallic/roughness/AO and the normal-map path | Added factor fields and albedo/normal/metallic/roughness/AO map inputs with clear actions, path validation, file drops and undo. Optional scene fields preserve factors and project-relative texture paths; old albedo-only scenes remain readable. Tests exercise edits/undo and actual model save/reload. | Linux image decoding supports PNG; Windows decoding follows the existing WIC backend. Windows and other GPU drivers remain untested. |
| Texture upload and normal-map lighting | CONFIRMED: OpenGL test bound texture zero for path-only textures; a neutral normal map changed a horizontal surface from pixel value 124 to 9 | Texture paths now use the existing BGUI decode/GPU cache. Tangent-space normals are transformed with a derivative-based tangent frame. Pixel tests verify neutral-map lighting and texture upload. Missing texture files fall back without terminating rendering. | No renderer architecture change. Degenerate UVs retain the mesh normal. Height/parallax controls are not exposed because the existing shader does not implement them. |
| Static backend dependencies | CONFIRMED by link failures in the new isolated rendering target | Declared the existing core/OpenGL/FreeType dependencies so static library consumers link correctly. | Build-only change; no public API removed. |
| Camera hover preview appearance | CONFIRMED: the hover preview was an unframed rectangular framebuffer image | Added a neutral dark card, camera icon/name, restrained border, rounded image and clipping. The card fits inside the viewport and accounts for UI scale. | Editor build and existing rounded-image OpenGL regression cover compilation/render support; final aesthetic review remains manual. |

Validation: a clean editor/runtime build from an isolated snapshot completed in `/tmp/cpp-bengine-ui-phase2-isolated-build`. All 46/46 CTest entries passed, including actual OpenGL texture/normal-map rendering and the rounded-image regression, with no display skips. The added white-map regression first failed (roughness pixel 255 instead of 124); metallic/roughness/AO maps now multiply their editable factors and roughness has a safe minimum. Logs: `/tmp/cpp-bengine-phase2-isolated-final.log` (reproduction) and `/tmp/cpp-bengine-phase2-isolated-tests-final.log` (final suite). Physical trackpad feel, final aesthetic review and Windows remain unvalidated. The pre-existing missing-return warning in `get_repository_root` remains outside this phase. Unrelated local native-dialog, persistence-load, preferences, Lua and FPP edits are preserved.

## UI polish — phase 3: scripts and text editing

| Item | Status and evidence | Change and tests | Validation limits / review |
|---|---|---|---|
| Creating and attaching scripts | CONFIRMED: Files had no creation action and only models/images were draggable | Added a Lua template action in the current folder; existing files and traversal names are rejected. Lua drops target geometry in Scene, the selected entity over empty Scene, Hierarchy rows or the Inspector script field. Syntax is checked without running user code; attachment/replacement/removal supports Undo. Script paths inside the project remain relative. `EditorScriptTest` covers creation, preservation, syntax rejection, locks and history. Workflow tests exercise the actual creation button, Lua file drag and geometry hit testing. | The existing one-script-per-entity component model is preserved. Drop over empty Scene requires a selected entity; the UI explains this. Physical mouse/trackpad interaction remains unvalidated. |
| Dirty marker and Ctrl+S | CONFIRMED: `EditorAssetWorkflowTest.CreatesScriptAndUpdatesDirtyAndSaveThroughTheUiLoop` failed because the file panel update was never invoked by the actual UI loop | Browser and document layouts now update after their children, including while another dock tab is active. Shortcut edge state is recorded before saving to prevent recursive saves. Per-file dock windows and title synchronization retain buffers and show the dirty marker in the dock tab; the overlapping text-editor work is preserved. | Automated tests exercise the UI update loop and inspect file contents. Markdown keeps its existing preview behavior. Native desktop review remains pending. |
| Text Undo affects scene history | CONFIRMED: `EditorAssetWorkflowTest.TextUndoDoesNotConsumeSceneHistory` consumed the scene action while an inputbox had keyboard focus | Scene Undo/Redo shortcuts now leave focused text editors to handle their own history. | Inputbox subclasses, including the Lua editor, are covered by the focus check. |
| Pixelated small text | PARTIAL: FreeType already retains antialiased coverage and the UI enables antialiasing; the 40px atlas explicitly disabled mipmaps despite rendering text around 11px | Enabled font-atlas mipmaps so the existing OpenGL sampler can filter reduction. `EditorRenderTest.SmallInterfaceTextUsesAntialiasedMipmappedGlyphs` first failed for both the atlas flag and missing GPU mip level; it now passes and also verifies partial glyph coverage. | This confirms and fixes the minification path; final perceived sharpness across display scales and other GPU drivers remains a visual review item. No font API or rendering architecture was replaced. |

Validation in progress: clean editor/runtime build and full tests in `/home/d4nl/.cache/cpp-bengine-ui-phase3-build`. Directed script, workflow and font tests have passed; scene/text Undo was reproduced separately before its fix. Other local project, native-dialog, preferences, Lua API/autocomplete and BGUI audit changes remain preserved.

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
| P0-a | CONFIRMADO (falha residual) e corrigido | O truncamento direto já não existia, mas `PersistenceTransactionTest.FailedProjectTemporaryOpenPreservesSavedScene` falhou antes da correção: a cena mudava de `Original` para `Changed` apesar de `save()` retornar `false`. | [project_file_transaction.hpp](../apps/editor/systems/project_file_transaction.hpp) prepara ambos os arquivos, publica registro com imagens anteriores, substitui os destinos e remove o registro como confirmação. Falhas detectadas restauram os anteriores; abertura/salvamento pelo mesmo caminho recupera uma transação interrompida. |
| P0-b | NÃO CONFIRMADO no código atual | Nomes são preparados em `staged_entity_names` e aplicados após o carregamento. `FailedStagingPreservesNamesAndActiveRegistry` e `InvalidInputsPreserveNamesAndActiveRegistry` passam. | Nenhuma correção de produção para nomes. Testes sem contexto OpenGL verificam nomes, visibilidade e bloqueios. |
| P0-c | NÃO CONFIRMADO no código atual nos cenários testados | O carregamento prepara outro ECS antes da aplicação. Os testes de falha preservam `entities`, `mascaras`, `live_entities` e identidade dos componentes; `SuccessfulLoadReplacesSceneAndRebindsComponents` verifica substituição e vínculos no sucesso. | Não foi simulada falha de alocação durante a aplicação final; não se afirma garantia forte para todas as exceções. |
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

## Native Browse dialogs (2026-10-27)

| Item | Status and evidence | Changes and tests | Validation limits / review |
|---|---|---|---|
| Custom Browse navigation | CONFIRMED by the directory-list implementations in `apps/editor_app.cpp` and `apps/editor/panels/editor_ui.cpp` | Replaced startup/project-manager file browsing and new-project/build folder browsing with `native_file_dialog`. Removed the embedded picker lists and their folder-navigation state. Recent projects, manual path entry, and `files_view.hpp` are preserved. | Windows uses `IFileOpenDialog`; Linux prefers KDialog on KDE, otherwise Zenity, falling back when the executable is absent. Linux requires one of those desktop programs (documented in README). |
| Cancellation, filtering, errors and path handling | CONFIRMED by five passing `NativeDialogTest` cases | Headless tests cover cancellation, project extensions, folder mode, spaces/Unicode/shell metacharacters, missing tools, backend failure, and Zenity-to-KDialog fallback. Paths are passed as separate process arguments without a shell. | Windows compilation/execution and interactive native-dialog behavior have not been validated on a desktop. Native dialogs are synchronous. No public engine API changed. |

Validation for this change (2026-10-27):

- Clean rebuild attempted with `cmake --build out-build-check --clean-first --parallel 2`, using the existing CMake configuration with editor, runtime, and tests enabled. `editor_app`, `runtime_app`, and BGUI `all_tests` compiled successfully. The full build failed linking `editor_render_tests` due to undefined BGUI symbols (`detail::log_out`, `detail::log_err`, `get_context_size`, `get_projection`); this target and its CMake configuration contain existing local work outside this change and were left untouched.
- `ctest --test-dir out-build-check -R '^DockTests$' --output-on-failure` passed; all 18 docking regressions pass, including the new test after the clean rebuild.
- Full CTest: 41/43 entries passed. `ProjectPersistenceTest.SavesRendererMaterialVisibilityAndLockState` failed at `tests/editor_persistence_tests.cpp:364` because serialized PBR data lacked `metallic`; `editor_render_tests_NOT_BUILT` could not run after the link failure. Neither issue was changed as part of the window fix. Consequently, a successful full build/test run remains pending.

Validation: configured a fresh `/tmp/cpp-bengine-native-dialog` build using the existing local GoogleTest source, then ran `bash scripts/build.sh /tmp/cpp-bengine-native-dialog --test`. Editor and runtime compiled. The complete build was blocked by unresolved BGUI symbols in the pre-existing local `editor_render_tests` target. Running CTest separately yielded 46 passed, one failed (`ProjectPersistenceTest.SavesRendererMaterialVisibilityAndLockState`: missing `pbr.metallic`) and one not run (`editor_render_tests_NOT_BUILT`), out of 48 entries. Those persistence/render test files were already locally modified before this change and were preserved. The five new native-dialog tests passed. Full green validation remains blocked by those unrelated failures; no stabilization diagnostic fixes were attempted in this phase.

## Menubar and interface zoom (2026-10-27)

| Item | Status and evidence | Changes and tests | Validation limits / review |
|---|---|---|---|
| Interface zoom shortcuts | CONFIRMADO: BGUI already exposed `get_global_scale`/`set_global_scale`, but its keyboard enum and GLFW mapping lacked `=`, `-`, `0` and keypad equivalents. | Added editor-only `ui_scale_system`, including the startup project manager. Ctrl+= / Ctrl+Shift+= / Ctrl+keypad-plus increase scale; Ctrl+- / Ctrl+keypad-minus decrease it; Ctrl+0 resets it. Steps are 10 percentage points, clamped to 50–200%, once per physical press. Five `UiScaleTest` cases cover modifiers, repeat suppression, keypad, reset, limits and pixel-element reflow. | The BGUI enum is extended at the end, preserving existing key values. Native keyboard delivery and visual rendering require desktop validation. Scale is session-only. |
| View and File menus | CONFIRMADO: existing menu actions were limited to project/UI persistence, build, undo/redo, lighting and physics. | Added View zoom actions, Standard/Wide Scene layout presets and moved existing UI persistence into Save/Load Layout. File exposes existing New Project and exit confirmation flows. `EditorMenuTest` exercises zoom menu actions and preset region/ratio changes. | Layout presets rearrange existing panels; they do not recreate closed panels. Existing project creation, saving and build implementations are reused. |
| Editor Preferences | CONFIRMADO: camera navigation values were private defaults and the scene grid was drawn unconditionally. | Added session preferences for grid visibility, movement speed, sprint multiplier, mouse sensitivity, scroll zoom sensitivity and smoothing. The Scene applies navigation settings on every update; gizmo drawing reads the grid toggle. Apply validates all fields atomically; Cancel leaves values unchanged; Reset Navigation restores defaults in the form. The modal body scrolls and adapts to viewport size and UI scale. `EditorMenuTest` covers application, invalid input, correction, cancellation and reset; `EditorPreferencesTest` checks each numeric range and finite-value requirement. | Preferences are session-only and do not change the project file format or public engine API. Visual grid and camera feel remain pending interactive verification. No P0/P1/P2 diagnosis fixes are part of this change. |

Validation: configured a fresh build with `cmake -S . -B /tmp/cpp-bengine-menubar -DBUILD_TESTING=ON -DBUILD_EDITOR=ON -DBUILD_RUNTIME=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/repos/cpp-bengine/out/_deps/googletest-src`, following the options in `scripts/build.sh` and CI while reusing the available GoogleTest source. The full build and the final incremental build after the last edits both succeeded with `cmake --build /tmp/cpp-bengine-menubar --parallel 2`, including editor and runtime. `ctest --test-dir /tmp/cpp-bengine-menubar --output-on-failure` passed all 62 CTest entries, including the 11 new zoom/menu/preferences cases. Logs are `/tmp/cpp-bengine-menubar-build-final.log` and `/tmp/cpp-bengine-menubar-ctest.log`. Interactive rendering, physical keyboard/layout delivery and Windows execution were not validated. Existing unrelated local work, including staged files and nested BGUI/backend edits, was preserved. No commit was created because these edits overlap files with staged work and concurrent changes belonging to other in-progress work; committing whole files would include that work.

## Auditoria BGUI: estrutura, bugs e desempenho (2026-10-27)

| Item | Status e evidência | Correção e testes | Limites e riscos para revisão |
|---|---|---|---|
| Propagação exponencial de invalidação de estilo | CONFIRMADO: `AuditTest.StyleInvalidationVisitsNestedLayoutsOnce` falhou antes da correção, com 2, 4, 8, …, 256 visitas nos oito níveis. `layout::mark_children_style_dirty` repetia a recursão já feita por `element::mark_style_dirty`. | Removida a segunda recursão em `external/cpp-bgui/core/include/lay/layout.hpp`. O teste exige uma visita por descendente e confirma que todos continuam com estilo sujo. | Elimina trabalho duplicado de estrutura; não mede FPS nem afirma complexidade linear de todo o processamento, que também invalida ancestrais. Sem mudança da API pública. |
| Leitura fora do buffer e aceitação de UTF-8 inválido | CONFIRMADO: `AuditTest.Utf8RejectsInvalidSequencesWithoutReadingPastInput` falhou com sequências truncadas, overlong, surrogates e valores acima de U+10FFFF. ASan reproduziu `heap-buffer-overflow` na linha 33 do `text.cpp` original com 64 bytes ASCII seguidos de 0xF0. | `external/cpp-bgui/core/source/elem/text.cpp` valida comprimento, bytes de continuação e escalares Unicode. Cada byte inválido vira U+FFFD e consome um byte, preservando offsets dentro da entrada. Regressão inclui texto válido, entradas inválidas e todos os bytes finais 0x80–0xFF em strings alocadas no heap. | Verificação isolada do decodificador com ASan/UBSan passou após a correção; não representa sanitização de toda a engine. |
| Cache reutiliza primitivas com recorte incorreto | CONFIRMADO: `AuditTest.RenderCacheTracksPrimitiveClipState` retornava vértices em 100 quando o recorte exigia no máximo 10; alterar somente o estado de `draw_list` não invalidava o cache. | A chave do cache em `element` inclui ativação e retângulo de recorte das primitivas, além do recorte de quads. Teste cobre ativar, alterar, desativar e reutilizar recorte idêntico. | Teste de geometria sem GPU; arquitetura de renderização preservada. |
| Teste existente ausente do CTest/CI | CONFIRMADO: `ContextMenuTest.ComputesPopupAndItemStylesWhenFirstOpened` estava compilado em `all_tests`, mas nenhum filtro de `tests/CMakeLists.txt` o selecionava; `ctest -N -R ContextMenu` listava zero entradas. | Registrados `ContextMenuTests` e a nova suíte `AuditTests`. Comparação dos nomes de suítes com os filtros não encontrou outras suítes BGUI omitidas. | O teste existente de menu de contexto passou sem mudanças no widget. |

Validação: configuração nova em `/tmp/cpp-bengine-bgui-audit`, com `BUILD_TESTING`, `BUILD_EDITOR` e `BUILD_RUNTIME` habilitados, reutilizando o GoogleTest local via `FETCHCONTENT_SOURCE_DIR_GOOGLETEST`. Build completa a partir dessa pasta nova concluída, seguida por `bash scripts/build.sh /tmp/cpp-bengine-bgui-audit --test` para validar os arquivos finais. CTest: **71/71 entradas passaram**. O executável BGUI `gl3_tests` também passou seu teste com contexto OpenGL real, sem skip. Três regressões novas falharam antes das correções e passaram depois. Logs: `/tmp/bgui-audit-before.log`, `/tmp/bgui-audit-build.log`, `/tmp/bgui-audit-final.log`, `/tmp/bgui-utf8-before-sanitizers.log` e `/tmp/bgui-utf8-sanitizers.log`.

Limites: Windows, Vulkan, interação manual do editor e desempenho de uma sessão completa não foram validados. A auditoria cobre os caminhos revisados de estilo/layout, texto, cache/recorte, widgets e registro dos testes; não certifica ausência de outros defeitos. O build apontou aviso de retorno ausente em `project_persistence_system::get_repository_root`, em código de persistência já modificado fora desta fase; ficou pendente para revisão separada. Alterações locais anteriores e concorrentes foram preservadas e não incluídas no commit de correção da BGUI. Nenhuma decisão de design pendente nas quatro correções acima.

## Confirmação de saída somente com alterações não salvas (2026-10-27)

| Item | Status e evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Aviso de salvar aparece mesmo após salvar | CONFIRMADO: `EditorMenuTest.SavedProjectExitsWithoutOpeningSaveConfirmation` falhou antes da correção (`exited == false` após salvar e chamar `confirm_exit`). | `confirm_exit` consulta `project_persistence_system::has_unsaved_changes` e encerra diretamente quando o estado coincide com o último salvamento/carregamento bem-sucedido. O teste passou após a correção e também verifica que uma edição posterior exige confirmação e que Save and Exit salva e encerra. | Menu File/Exit e fechamento da janela usam o mesmo caminho. Interação manual de desktop e Windows não foram validados. |
| Detectar alterações além do histórico de undo/redo | CONFIRMADO: os dados persistidos incluem propriedades e configurações que podem ser alteradas diretamente. | A serialização existente é compartilhada com uma comparação em memória, sem gravar durante a consulta. Snapshots são separados por caminho e atualizados somente após save/load bem-sucedido. Objetos JSON são comparados por conteúdo e entidades são ordenadas por ID no snapshot, evitando diferenças por ordem dos mapas. `ProjectPersistenceTest.TracksSavedStateAcrossEditsUndoLoadsAndTemporaryExports` cobre nomes, locks, gravidade, transform, retorno ao estado anterior, criação/remoção, load, falha de save e exportação temporária. | O formato dos arquivos permanece igual; a API pública recebe apenas um método adicional. Mudanças fora dos dados serializados (como preferências de sessão) não exigem salvar o projeto. |
| Projeto nunca salvo e cancelamento | CONFIRMADO por `EditorMenuTest.UnsavedProjectRequiresConfirmationAndCancelKeepsEditorOpen`. | Ausência de snapshot ou erro ao serializar mantém a confirmação. Cancel não fecha o editor; Discard and Exit fecha. | Erros de salvamento não atualizam o snapshot e não marcam o projeto como salvo. Nenhum item P0/P1/P2 foi corrigido nesta fase. |

Validação desta fase: os três testes específicos passaram. Build limpa completa e suíte CTest em andamento em `/tmp/cpp-bengine-menubar`; resultado final será registrado abaixo. Alterações locais e concorrentes de outras fases foram preservadas.

## Pausa ao alterar a escala da interface (2026-10-27)

| Item | Status e evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Invalidação repetida durante o zoom | CONFIRMADO: o teste de uma árvore com 16 níveis falhou antes da correção; o último painel recebeu 18 visitas na cascata de estilos. | `element::compute_style` propaga alterações somente aos filhos ainda limpos, preservando a invalidação dos filhos já pendente. `AuditTest.ScaleCascadeDoesNotRepeatedlyInvalidateDirtyDescendants` limita a três visitas por painel e verifica a escala calculada. | Remove trabalho redundante na cascata, sem mudar a API pública nem a arquitetura de renderização. Não representa uma medição de FPS ou da duração da pausa no editor completo. |
| Escala aplicada no quadro seguinte ao atalho | CONFIRMADO: teste antes da correção manteve geometria em 100×40 após a escala mudar para 110%, em vez de 110×44. | `bgui::on_update` resolve novamente os estilos quando a escala muda durante os eventos/updates, antes de medir e organizar a geometria. `AuditTest.ScaleChangeDuringUpdateReflowsInTheSameFrame` cobre esse caminho usado pelo atalho. | A nova cascata só ocorre quando a escala realmente mudou durante o update. |
| Zoom no limite invalida toda a interface sem alterar a escala | CONFIRMADO: o teste anterior deixou a raiz com estilo sujo ao reaplicar 200%. | `set_global_scale` retorna imediatamente se o valor for o atual. `AuditTest.UnchangedScaleDoesNotInvalidateStyles` cobre a reaplicação. | Limites e passos dos atalhos são preservados. |
| Travada relatada na sessão interativa | PARCIAL: os caminhos redundantes acima foram reproduzidos; a pausa visual da sessão do usuário não foi medida diretamente. | Três regressões sem GPU acompanham as correções na suíte BGUI `AuditTests`. | Interação manual com Ctrl+/Ctrl−, Windows e Vulkan não foram validados. A latência percebida precisa de revisão no desktop; nenhum item P0/P1/P2 foi tratado nesta fase. |

Validação desta fase: build limpa em pasta nova `/tmp/cpp-bengine-zoom`, configurada com editor, runtime e testes habilitados e GoogleTest local; execução de `bash scripts/build.sh /tmp/cpp-bengine-zoom --test` em andamento. Resultado final será registrado abaixo. Alterações locais anteriores e concorrentes foram preservadas.

## Revalidação P0-a–c — 2026-10-27

Escopo: somente persistência. Alterações locais preexistentes foram preservadas.
A reprodução inicial adicionou seis testes: cinco passaram e um confirmou a
perda da cena anterior quando a abertura de `project.bengine.tmp` falha.
Evidência: `/tmp/bengine-p0abc-reproduction-tests.log`. O build completo inicial
passou; CTest teve 88/92 sucessos. Além da reprodução, falharam três testes de
alterações locais anteriores: `EditorAssetWorkflowTest.TextUndoDoesNotConsumeSceneHistory`,
`LuaApi.UnityStyleTransformAndLegacyApiCoexist` e
`EditorDocuments.DockTabsPreserveBuffersAndSaveWithShortcut`.

O usuário escolheu explicitamente recuperação após interrupção (regra 6).
Foi introduzido um auxiliar interno de transação, sem mudar a API pública ou os
formatos de projeto/cena. Todos os documentos são serializados antes da escrita.
Os temporários ficam ao lado dos destinos; `<caminho-de-entrada>.transaction`
armazena os conteúdos anteriores e a ausência de arquivos ainda não existentes.
O registro é publicado por renomeação antes da primeira substituição. Sua remoção
é o ponto de confirmação: interrupções anteriores restauram a versão completa
anterior; interrupções posteriores mantêm a nova. A restauração é repetível e
preserva o registro se não conseguir terminar. Registros inválidos impedem a
abertura sem alterar o ECS. Windows usa `MoveFileExW` para substituir arquivos
existentes; Linux usa `rename`.

Testes adicionados em `tests/editor_persistence_transaction_tests.cpp`:
- falha ao preparar temporários de cena/projeto e componente não serializável;
- JSON inválido, referência ausente, entidade inválida e rejeição após iniciar
  a preparação do ECS, preservando nomes, mapas, IDs e câmera;
- carregamento bem-sucedido com componentes vinculados ao ECS ativo;
- encerramento de subprocessos em cada etapa: preparação, publicação do registro,
  primeira/segunda substituição e confirmação;
- interrupção durante a recuperação, nova tentativa após erro de recuperação,
  falha real de substituição por temporário ausente e primeiro salvamento interrompido;
- cena avulsa, salvamento após transação pendente, registro corrompido, caminhos
  auxiliares sobrepostos e tentativa de restaurar arquivo alheio à cena declarada.

Validação final: build limpo isolado de editor, runtime e testes concluído;
**70/70 testes passaram**, incluindo os **17 novos testes** de persistência, sem
nenhum teste ignorado no Linux. A fonte isolada contém somente esta fase sobre
HEAD, com submódulos nas revisões registradas (inclusive o backend da BGUI).
A integração com a árvore de trabalho atual também compilou: **100/103 testes
passaram**, permanecendo exclusivamente as mesmas três falhas locais anteriores
listadas acima. Nenhuma dessas alterações locais foi incluída no commit desta fase.
Comando de CI: `bash scripts/build.sh <pasta> --test`; GoogleTest usa a cópia local
já existente em `out/_deps/googletest-src`. Logs:
`/tmp/bengine-p0abc-clean-tests.log` (isolado) e
`/tmp/bengine-p0abc-final-workspace.log` (árvore com alterações locais).

Limites e riscos: recuperação após **interrupção do processo**, não garantia de
durabilidade contra queda de energia (não há `fsync`/`FlushFileBuffers`). A
recuperação procura o registro pelo caminho de entrada usado para salvar:
reabrir o projeto recupera seu par projeto/cena; abrir uma cena avulsa recupera
seu próprio registro, mas não procura registros de outros projetos que a usem.
Não há suporte a escritores simultâneos. Os sufixos `.tmp` e `.transaction` são
reservados; destinos com links simbólicos, múltiplos hard links ou sobreposições
são recusados. Windows, falha real de disco cheio, falhas de alocação e revisão
visual do editor/FPP não foram validados. Os testes novos não criam contexto
OpenGL. Os sete testes que encerram subprocessos usam `fork` e são explicitamente
ignorados no Windows; os demais testes não exigem esse recurso. O aviso preexistente de retorno ausente em `get_repository_root` permanece
fora desta fase.

## API Lua e edição de texto — 2026-10-27

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| API Lua próxima da Unity | CONFIRMADO: `code::setup()` expunha somente tabelas `bubble.*`; escopo escolhido pelo usuário: base, preservando compatibilidade | `gameObject`, `GameObject.Find(id)`, `GetComponent`, Vector3, Transform, Time, Input e Debug. `LuaApi.*` cobre acesso ao ECS, movimentação, API antiga, tempos e entidades removidas. Detalhes em `docs/LUA_API.md`. | Subconjunto explícito: busca por ID; Transform, Renderer e Rigidbody; sem novos callbacks de ciclo de vida. Entrada real por teclado/janela e Windows exigem validação interativa. |
| Exemplo FPP migrado | CONFIRMADO: os três scripts usavam exclusivamente `bubble.*`; a cena atual identifica PlayerCapsule como 106 e PlayerCamera como 100 | Scripts agora usam a API nova. `Input.GetMouseDelta`, `Renderer.enabled`, consulta a Rigidbody e `Destroy` mantêm movimento, câmera e coleta sem chamadas antigas. `LuaApi.FppScriptsMoveLookAndCollectUsingTheNewApi` executa os scripts reais com entrada simulada. | A convenção de yaw existente (+X em zero) foi preservada; reprodução interativa completa do jogo ainda pendente. |
| Rolagem multilinha | CONFIRMADO: `inputbox` era um `linear` horizontal, enquanto a rolagem é vertical; regressão `TextEditor.MultilineCanScrollWithoutFocus` | Campo multilinha usa orientação vertical. Teste sem GPU usa fonte sintética e verifica deslocamento do texto. | Rolagem vertical; não foi adicionada uma barra horizontal à BGUI. |
| Guias e botões duplicados | CONFIRMADO por `files_view::activate_document()` e `ensure_document_window()` | Guias desabilitadas e removida a barra com Salvar/botões de arquivo. Arquivos usam abas próprias da dock, mantendo buffers/undo ao alternar. `EditorDocuments.*` cobre abertura, troca e Ctrl+S. | Markdown continua como visualização, conforme comportamento existente. Revisão visual interativa pendente. |
| Asterisco na dock | CONFIRMADO: `dock::ensure_tab_controls()` copiava o título apenas ao criar a aba; regressão `TextEditor.DockTabFollowsWindowTitle` | Rótulo acompanha mudanças de título; `*` depende da comparação com o conteúdo salvo. Teste de integração verifica remoção após salvar. | Títulos ainda são usados pelo mecanismo existente de persistência da dock; identidade persistente independente de nome fica fora desta fase. |
| Autocomplete Lua pequeno | CONFIRMADO como recurso novo solicitado | Catálogo da API do motor, sugestões após `.`/`:`, filtro por prefixo, aliases simples de Transform/GameObject, ↑/↓, Tab/Enter, Esc, clique e desfazer. `LuaCompletion.*` cobre resolução, supressão em comentários/strings, teclado e foco após clique. | Não é um servidor de linguagem: sem resolução geral de módulos, classes do usuário ou escopos complexos. Catálogo precisa acompanhar adições à API. |

Validação: build novo de editor/runtime em `build/lua-editor`, seguindo
`bash scripts/build.sh build/lua-editor --test`, e reexecução completa de CTest:
**110/110 entradas passaram**. Os 12 testes desta fase passaram, incluindo a
execução dos scripts reais do FPP com entrada simulada. A versão isolada do
patch de edição também passou na checagem de sintaxe C++.

O teste local `TextUndoDoesNotConsumeSceneHistory` foi ajustado para simular
foco real com `bgui::focus_keyboard`, em vez de alterar somente a flag do campo.
O aviso preexistente de retorno ausente em `get_repository_root` permanece fora
desta fase. Windows, interação visual da caixa de sugestões e uma partida
interativa completa do FPP não foram validados. Alterações locais dos demais
trabalhos foram preservadas.

## Correções de interação e apresentação — etapa 1 (2026-10-27)

| Item | Status / evidência | Alteração e cobertura | Limites / riscos |
|---|---|---|---|
| Traffic lights cortados | CONFIRMADO: `DockTest.TrafficLightsFitInsideControlsAtEveryScale` falhava com os botões fora do grupo em 1x, 1.5x e 2x. | `traffic_light` agora exige modo pixel, coerente com seu tamanho explícito. Teste passa nas três escalas. | Conferência visual no editor ainda pendente; sem mudança de API. |
| Recuo nos documentos | CONFIRMADO: `EditorDocuments.DockTabsPreserveBuffersAndSaveWithShortcut` reproduziu x=11 contra x=0 do contexto. | Removidos paddings acumulados, margem do texto e padding do input; status movido para baixo. Asserções de geometria passam. | O teste completo também passa após a atualização concorrente de foco de abas na BGUI; a busca de botões foi limitada ao contexto do documento, excluindo os controles desabilitados do cabeçalho. |

## Correções de interação e apresentação — etapas 2 e 3 (2026-10-27)

| Item | Status / evidência | Alteração e cobertura | Limites / riscos |
|---|---|---|---|
| Imagens invertidas | CONFIRMADO: `ImageTest.FileTexturesUseTopLeftUVAndFramebuffersRemainFlipped` falhou antes da correção; imagens de arquivo recebiam UV vertical invertida apesar de decodificadas do topo para baixo. | UV normal para arquivos; textura externa mantém inversão padrão e opção explícita. `set_texture` restaura a orientação ao trocar a origem. | Verificação de comandos de desenho sem GPU; framebuffers da cena conservam a convenção anterior. |
| Modificador do trackpad | CONFIRMADO: `CameraModifierUsesOnlyLeftControlAndRespectsTextFocus` falhou antes da correção. | Space substituído por Ctrl esquerdo, inclusive na ajuda e no teste de navegação; foco em edição de texto continua bloqueando o atalho. | Ctrl direito não ativa navegação; atalhos de texto preservados. |
| Gizmo fora da cena | CONFIRMADO: `GizmoContinuesWhenItsCenterLeavesViewport` reproduziu posição estacionária em x=1.8 no segundo movimento externo. Havia também perda de captura no adaptador do editor. | Arraste ativo continua fora dos limites e da janela até soltar o botão; início permanece condicionado à cena. Cobertura do núcleo e integração com desfazer. | Renderização continua recortada; projeção atrás da câmera permanece sem manipulação até retornar à frente. |
| Inversão angular da câmera | CONFIRMADO: `CameraForwardRemainsContinuousAcrossYawNinetyDegrees` falhou antes da correção em yaw 89/91/179/181/269/271/359/361 com pitch 20. | Câmera sem pai usa seus ângulos contínuos diretamente, evitando decomposição matricial ambígua. | Câmeras com pai mantêm comportamento anterior; navegação usa câmera do editor sem pai. Sem alteração da API pública. |

## Correções de arraste e resize — etapa 4 (2026-10-27)

| Item | Status / evidência | Alteração e cobertura | Limites / riscos |
|---|---|---|---|
| Resize inexato | CONFIRMADO: `DockTest.FloatingWindowDragAndCornerResizePublishExactGeometryInSameFrame` falhou no canto superior esquerdo: X era sobrescrito ao atualizar Y e tamanho só mudava no quadro seguinte. | Coordenadas calculadas conjuntamente; estilo do tamanho resolvido antes do arrange do mesmo quadro. Teste cobre cinco movimentos e cinco resizes consecutivos em 1x, 1.5x e 2x, conservando as bordas opostas. | Respeita limites mínimos/máximos e escala existentes. |
| Interior atrasado ao arrastar | PARCIAL: conteúdo aninhado, incluindo label, acompanha a janela no mesmo quadro no teste de geometria; usuário identifica principalmente cena e imagens. | Mantida cobertura de arraste e investigado ciclo das texturas abaixo. | Não foi reproduzido atraso geral de todos os elementos; sem correção especulativa no layout. |
| Artefatos na cena durante resize | CONFIRMADO no fluxo: `scene_view_runtime::on_update` chamava `viewport_sync` depois de `render_system`, fazendo `glTexImage2D` descartar a imagem renderizada antes de exibi-la. O preview também alocava durante atualização da UI. | UI solicita o tamanho; a câmera só realoca no início do próximo `drawFB`, antes de renderizar. Cena e preview mantêm os pixels prontos até esse momento, inclusive após reabrir a janela. Teste OpenGL `DeferredViewportResizePreservesRenderedPixelsUntilNextCameraPass` verifica preservação do armazenamento, todos os pixels após três tamanhos, pedidos inválidos e primeira alocação do preview. | A geometria publicada do quadro anterior define a resolução do próximo render, evitando descartar pixels prontos; pode haver ajuste de resolução de um quadro durante resize. Validação visual manual ainda pendente. |

Validação final destas quatro etapas: build de editor/runtime a partir da pasta
nova `build/interaction-clean`, seguindo `bash scripts/build.sh
build/interaction-clean --test`. Após recompilar todas as dependências da câmera,
**110/110 entradas CTest passaram**, sem skips, incluindo os testes OpenGL da
BGUI e `DeferredViewportResizePreservesRenderedPixelsUntilNextCameraPass`.
Esse último teste também falhou com o resize imediato anterior (armazenamento
mudava de 32 para 40 pixels durante a atualização da UI). Os testes de imagem
verificam ainda que o retângulo enviado ao desenho acompanha o arraste no mesmo
quadro. Log final: `/tmp/bengine-verified-final.log`; reprodução do descarte de
framebuffer: `/tmp/bengine-framebuffer-red.log`.

Windows, sensação física do trackpad e revisão visual interativa de arraste e
resize continuam sem validação. O atraso geral de conteúdo não foi reproduzido
nos testes de geometria; os artefatos confirmados de framebuffer foram tratados.
O aviso preexistente de retorno ausente em `get_repository_root` permanece fora
deste escopo. A API existente foi preservada; `camera::request_framebuffer_size`
é uma adição para solicitar resize sem descartar os pixels do quadro pronto.
Alterações locais anteriores e concorrentes foram preservadas nos commits finais.
