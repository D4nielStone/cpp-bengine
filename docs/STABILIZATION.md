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


## Gizmo de rotação — 2026-10-28

| Item | Status / evidência | Alteração e cobertura | Limites / riscos |
|---|---|---|---|
| Anéis giram com o objeto e giro incorreto em objeto inclinado | CONFIRMADO: `RotationGizmoUsesWorldQuaternionAndKeepsWorldRings` falhou antes da correção, comparando a matriz final com um giro mundial de 90°. O código usava eixos orientados pelo objeto e somava Euler. | Anéis de rotação em eixos mundiais; composição por quaternion normalizado a partir da orientação inicial. Conversão final respeita a ordem Rx·Ry·Rz do Transform, preservando API, Inspector e formato das cenas. Teste inclui projeção em perspectiva e verifica que o clique inicial preserva exatamente os valores anteriores, usados pelo desfazer. `WorldRotationGizmoRespectsRotatedParent` cobre pai rotacionado. | Apenas o gizmo usa quaternion internamente; armazenamento público continua em Euler. Escalas não uniformes em ancestrais podem introduzir shear, que o Transform existente não representa. |
| Ângulo do mouse em anéis oblíquos | CONFIRMADO por leitura: `atan2` da tela era usado para qualquer plano. | Ângulo obtido pela interseção do raio do mouse com o plano mundial selecionado. `RotationUsesPlaneAngleForObliqueRingAndAccumulatesTurns` cobre projeção oblíqua, passagem por ±π e mais de uma volta. `EdgeOnRotationRemainsFiniteAndReversesWithDrag` cobre arraste reversível em anel exatamente de perfil. | De perfil, o plano não tem interseção única: usa tangente projetada e sensibilidade de 74 pixels/radiano. Movimentos maiores que meia volta entre eventos não têm sentido unívoco. |
| Indicador de relógio | CONFIRMADO: solicitado pelo usuário e ausente no desenho anterior. | Setor translúcido, arco e ponteiros inicial/atual no plano do anel, recortados às regiões visíveis. `RotationClockShowsSweepAndDisappearsOnRelease` verifica os vértices do setor e seu desaparecimento ao soltar. | Após uma volta, o preenchimento permanece completo; o ponteiro continua seguindo o ângulo acumulado. Revisão visual interativa ainda pendente. |

Validação desta etapa: build de editor/runtime iniciado em diretório novo pelo
comando do CI `bash scripts/build.sh build/rotation-clean --test` e recompilação
final após os últimos ajustes: **116/116 entradas CTest passaram, sem skips**.
Os cinco testes específicos não criam contexto OpenGL. Log final:
`/tmp/bengine-rotation-final.log`. A primeira execução completa detectou também
alteração mínima dos Euler no clique inicial; a versão final só aplica rotação
após mover o mouse, preservando os valores exatos do snapshot de desfazer.
Windows e revisão visual/manual do indicador e da sensação de arraste não foram
validados. Escalas não uniformes em ancestrais permanecem limitadas pelo modelo
TRS existente. O aviso preexistente de retorno ausente em `get_repository_root`
apareceu no build novo e permanece fora do escopo.
Não foram alterados os arquivos locais do projeto FPP.

## Visualização orbital e rotação FPP — 2026-10-28

| Item | Status / evidência | Alteração e cobertura | Limites / riscos |
|---|---|---|---|
| Alternar navegação normal/orbital | CONFIRMADO: havia suporte interno a pivô, mas nenhuma opção na Scene. | Botão `View: Normal` / `View: Orbital`, com ícone válido `eye`, em `tools_container` sobreposto ao viewport. Orbital usa a posição mundial do objeto selecionado como pivô e distância inicial proporcional ao raio mundial da malha (ou escala, sem malha), considerando FOV. Selecionar outro objeto recalcula o enquadramento; mover o objeto atualiza o centro. Scroll ajusta a distância. `SelectedOrbitUsesWorldPivotAndObjectRelativeDistance` cobre hierarquia, escala e acompanhamento. A API de órbita manual permanece coberta por `SwitchingOrbitalAndNormalPreservesEyeAndChangesDragPivot`. | Modo mantido durante a vida do painel; não é preferência persistida. Sem seleção, navegação normal até selecionar um objeto. O enquadramento orbital usa esfera centrada na origem do objeto. |
| Arraste direto no viewport | CONFIRMADO: rotação exigia botão direito ou Ctrl + botão esquerdo. | Botão esquerdo arrasta a câmera sem Ctrl; gizmo ativo impede a rotação. Picking aguarda soltar e cancela seleção em arraste maior que quatro pixels escalados. Toolbar não captura navegação nem picking. `PrimaryDragRotatesWithoutSelectingAndGizmoBlocksCamera` cobre rotação, seleção preservada e exclusão do gizmo. | Clique curto seleciona ao soltar. Ctrl + arraste e botão direito continuam disponíveis. |
| Câmera FPP inverte em certos ângulos | CONFIRMADO: `ParentedCameraKeepsPitchAndYawAcrossFullTurns` falhou antes da correção. A câmera filha usava decomposição Euler do Transform mundial. | Direção local transformada pela composição quaternion das rotações ancestrais, sem decomposição Euler e sem influência da escala. Teste cobre yaw positivo/negativo, passagens por 90°, 180°, 360° e várias voltas mantendo pitch. | Convenção local das câmeras existentes preservada (+X em yaw zero). Pai segue rotação geométrica do Transform. Up continua alinhado à referência vertical mundial. |
| Controles do exemplo FPP | CONFIRMADO: script tentava indexar `.x` no primeiro número retornado por `GetMouseDelta`. Movimento também não coincidia com a base da câmera filha. | Leitura do primeiro retorno numérico; yaw com sinal correspondente à câmera herdada; WASD usa a mesma base horizontal. Teste existente `FppScriptsMoveLookAndCollectUsingTheNewApi` atualizado para a direção herdada e aprovado. | Orientação inicial das cenas existentes pode parecer diferente porque passa a seguir a rotação real do pai. Arquivos da cena e configurações locais foram preservados. |

Validação: build de editor e runtime concluído em diretório novo com
`bash scripts/build.sh build/orbit-clean --test`. CTest: **134/135 passaram**;
a única falha é `InspectorComponentsAreIndependentTreesAndAddButtonIsLast`,
preexistente nas alterações locais de outra tarefa (botão Add Component não
encontrado). Os testes desta etapa e o teste Lua do FPP passaram. Log completo:
`/tmp/orbit-clean-final.log`. Revisão visual/manual da navegação e compilação
Windows não executadas nesta etapa. O aviso preexistente de retorno ausente em
`get_repository_root` permanece fora do escopo.

## Debug, propriedades Lua e luz direcional — 2026-10-28

| Item | Status / evidência | Alteração e testes | Limites / riscos |
|---|---|---|---|
| Fundo do Debug | CONFIRMADO: teste `DebugSharesFilesTabAndDisplaysScriptHistory` falhou na ausência da classe de superfície escura. | Janela e conteúdo usam `editor-surface`, já definido no tema escuro. O teste cobre histórico, grupo de abas e botão Limpar (processando a fila de callbacks BGUI). Integração local de logs previamente em andamento foi preservada. | Cor validada pela configuração do tema/classe; revisão visual com GPU pendente. |
| Erros ao iniciar o jogo | CONFIRMADO: `SyntaxAndSetupErrorsAppearAtGameStart` registrava erro de sintaxe, mas falhava para erro dentro de `setup()`. O resultado protegido da função Lua era descartado. | `code::setup()` e `code::update()` verificam o resultado e encaminham `sol::error` ao sistema de logs existente. Testes cobrem sintaxe, setup, update, desativação após falha e transporte de registros completos/multilinha entre runtime/editor. | Transporte testado por arquivo sem executar janela do runtime; revisão visual do fluxo Play e Windows pendentes. |
| Direção da luz baseada no Transform | CONFIRMADO: teste de pai rotacionado falhou quando o cálculo retornava a direção local, como o render anterior. | `get_world_direction()` compõe rotações Rx·Ry·Rz do objeto e ancestrais. Render usa o resultado tanto na luz global quanto na luz da câmera. Dois testes cobrem hierarquia, múltiplos eixos, posição, escala não uniforme/zero e preservação da direção local/magnitude. | Decisão do usuário: girar a direção configurada, sem substituí-la por eixo frontal. Posição e escala não afetam a direção. Validação dos pixels iluminados pendente. |
| Propriedades modificáveis por Lua | CONFIRMADO: `ComponentPropertiesAreWritable` falhou ao atribuir massa ao Rigidbody. | GetComponent e propriedades para Camera, DirectionalLight, PointLight; Rigidbody com massa, atrito, restituição, velocidade e métodos de força/fatores; Renderer mantém enabled e aceita visible. Teste verifica alterações nos componentes e no corpo Bullet; autocomplete e documentação atualizados. | Mantidas APIs existentes. Vetores do Transform continuam sendo cópias e exigem atribuição do vetor completo; recursos GPU/modelos não são ponteiros editáveis por Lua. |
| Espaço extra no rodapé do texto | CONFIRMADO: teste de limite inferior do inputbox falhou antes da correção. A linha de status vazia reservava altura/padding. | Linha de status só fica habilitada quando há mensagem; `DockTabsPreserveBuffersAndSaveWithShortcut` verifica o editor ocupando o conteúdo até a borda inferior. | Mensagens reais, como confirmação de salvamento, continuam ocupando a linha de status. Sem alteração na BGUI. |

Validação final do build limpo e CTest será registrada após a conclusão.
Alterações locais alheias a esta etapa, incluindo cenas FPP e navegação da Scene,
foram preservadas. O teste FPP acompanha a mudança local já existente de sinal do
movimento/yaw; o script não foi alterado nesta etapa.

## Persistência da interface e Reset UI — 2026-10-28

| Item | Status / evidência | Alteração e testes | Limites / riscos |
|---|---|---|---|
| Restaurar a interface ao reabrir | CONFIRMADO por leitura: havia somente Save/Load Layout manuais, sem chamada no início/fim do editor e sem registro dos documentos abertos. | Layout em `.editor-ui.cfg` e sessão em `.editor-session.cfg`, na pasta do projeto. Recria documentos antes de restaurar docking, divisões, abas ativas e janelas flutuantes. Guarda diretório do navegador, zoom e rascunhos de texto, sem sobrescrever arquivos de origem. Salva também antes da troca de projeto. `SessionRestoresOpenFilesDirtyBuffersAndScale` cobre reabertura, rascunho multilinha, nomes com espaços, zoom, layout flutuante, substituição da sessão e arquivo removido. | Encerramento abrupto não salva a sessão; arquivos removidos são ignorados. Estado da janela nativa do sistema operacional e posição do cursor não fazem parte desta implementação. |
| View > Reset UI | CONFIRMADO por leitura: ação ausente. | Restaura layout Standard e zoom 100%, preservando documentos e seus buffers. Save/Load Layout manuais usam o mesmo arquivo por projeto. `ResetUiRestoresStandardLayoutAndScaleWithoutClosingDocuments` cobre layout, zoom e preservação das janelas. | Revisão visual/manual e Windows pendentes. |

Validação: compilação limpa de editor/runtime concluída em `build/ui-session-clean`
pelo script do CI. Resultado final: **134/135 entradas CTest aprovadas**, incluindo
os dois testes novos e os testes existentes de documentos/menus. A única falha é
`InspectorComponentsAreIndependentTreesAndAddButtonIsLast`: procura o rótulo
`+ Add Component`, enquanto a alteração local do Inspector usa `Add Component`.
Nenhum desses dois trechos foi alterado nesta tarefa. Logs em
`/tmp/bengine-ui-session-build.log` e `/tmp/bengine-ui-session-ctest.log`.
Windows e revisão visual/manual não foram executados. Não foi criado commit:
o índice já continha alterações simultâneas de outras tarefas nos mesmos arquivos;
foi preservado para não incluir trabalho alheio.

### Busca de objetos por nome em Lua

| Item | Status / evidência | Alteração e testes | Limites / decisões |
|---|---|---|---|
| `GameObject.Find("nome")` | CONFIRMADO: `FindAcceptsObjectNamesAndKeepsNumericLookup` falhou com argumento string porque a função aceitava apenas ID numérico. | Overload por nome; metadados de nomes no ECS carregados junto à cena, preservados no save sem contexto de editor e removidos ao destruir entidade. Testes cobrem nome exato, distinção entre nome numérico e ID, ausência, duplicação, remoção/reuso de ID, metadados órfãos, uso do Transform encontrado, carregamento no runtime, busca durante setup e rollback em load inválido. | Decisão explícita do usuário: nomes duplicados retornam `nil`. Comparação diferencia maiúsculas/minúsculas e nome vazio retorna `nil`. Usa nomes atribuídos/salvos na cena; rótulos automáticos de exibição do editor não são nomes de busca. API por ID preservada. |


## Importação por arrasto no editor (2026-10-29)

| Item | Status e evidência | Mudança e testes | Limites e revisão |
|---|---|---|---|
| Arquivos externos no painel Arquivos | CONFIRMADO: `window` não registrava `glfwSetDropCallback`; o teste `ExternalFilesAreCopiedWithoutOverwritingAndAppearInBrowser` inicialmente não compilou pela ausência de `import_file`. | Entrega nativa em fila, cópia para `assets/`, seleção/atualização do painel, nomes com sufixo sem sobrescrever arquivos e reutilização de arquivos já presentes no projeto. Testes verificam conteúdo original/copiado, arquivo ausente e entrega de caminhos UTF-8/coordenadas. | Diretórios não são importados. A cópia para o projeto permanece após desfazer uma alteração da cena. |
| Aviso “Arraste aqui” | PARCIAL quanto às plataformas: faltava feedback para arrasto externo. | Camada escura semitransparente, texto centralizado e sem captura de entrada; desaparece ao sair/soltar e durante execução/modal. Detecção externa por X11 e OLE no Windows; arrastos internos usam o mesmo aviso. Testes isolados de estado, emissão de desenho e leitura real do framebuffer. | Windows/OLE não compilado nem executado neste host. Wayland/macOS recebem a soltura por GLFW, mas ainda não têm notificação antecipada para o aviso externo. Arrasto real a partir de gerenciadores de arquivos distintos exige revisão manual. |
| Modelos, imagens e scripts sob o cursor | CONFIRMADO por leitura: o modelo era criado na origem; imagem só tinha alvos no Inspector; script na cena vazia podia atingir a seleção anterior. | Modelo segue o raio do cursor até o chão `y=0`; se não houver interseção à frente, usa distância de 5 unidades no raio. Imagem muda o albedo dos materiais somente do objeto atingido, com desfazer/refazer. Script usa o objeto sob o cursor ou a linha da hierarquia, com compilação sem execução e caminho relativo ao projeto. Testes de projeção, limites do painel, imagem/alvo/bloqueio e script importado com desfazer/refazer. | Mantidos os formatos de imagem existentes: PNG no Linux; formatos WIC no Windows. Soltar uma imagem em área vazia somente importa o arquivo. |
| Portabilidade dos materiais de modelos importados | CONFIRMADO: `ImportedModelUsesCopiedNestedTextures` falhou procurando `assets/color.png` em vez de `assets/model/textures/color.png`. | Importação acompanha `.mtl`, `.bin` e texturas externas descobertas pelo Assimp, preservando referências relativas e separando colisões em pastas próprias. `model::load_texture` conserva subpastas relativas ao modelo. Testes removem a origem e verificam leitura pelo Assimp e carregamento do modelo com OpenGL. | Referências absolutas são recusadas com mensagem para evitar dependência silenciosa da origem. Formatos/recursos não suportados pelo carregador existente, incluindo texturas embutidas, não foram ampliados. |
| Alvo do arrasto durante redimensionamento | CONFIRMADO: `SceneDropFindsTheVisibleEntityWithoutChangingSelection` falhou ao usar framebuffer de 800×800 para painel de 200×200. | Conversão do cursor para o tamanho efetivo do framebuffer antes do raycast; o teste cobre entidade visível, bloqueada, oculta e ausência de mudança da seleção ao consultar o alvo. | Sem mudança da API de seleção ou da arquitetura de renderização. |

Validação: build novo de editor/runtime em `/tmp/cpp-bengine-file-drop-20261029`
e **151/151 entradas CTest passaram**, incluindo os testes OpenGL sem skips.
Comandos derivados do CMake/CI: `cmake -S . -B /tmp/cpp-bengine-file-drop-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON`,
`cmake --build /tmp/cpp-bengine-file-drop-20261029 --config Release --parallel 1`,
`ctest --test-dir /tmp/cpp-bengine-file-drop-20261029 --output-on-failure -C Release`.
A configuração de gerador único não define `CMAKE_BUILD_TYPE`, como o helper atual da CI;
`--config Release` não implica otimização Release nesse gerador.
Logs: `/tmp/bengine-file-drop-clean.log`, `/tmp/bengine-file-drop-final-build.log`
e `/tmp/bengine-file-drop-final-tests.log`. O teste gráfico inicialmente revelou
que o layout raiz reduzia o aviso a 15×15; a camada agora solicita explicitamente
toda a área da janela e o teste verifica dimensão, escurecimento e remoção.
As alterações locais preexistentes em iluminação, Inspector, Lua e projeto FPP foram preservadas.

## Recursos essenciais — cobertura de contratos (2026-10-29)

Fase limitada a testes e documentação. Nenhuma mudança de produção/API ou correção
especulativa das hipóteses do AGENTS.md. Matriz das bases e suítes existentes em
`tests/README.md`; novo executável `engine_foundations_tests` registrado no CTest.

| Item | Status / evidência | Mudança e testes adicionados | Limites / revisão |
|---|---|---|---|
| ECS / P0-d, P0-e, P0-f | NÃO CONFIRMADOS nos cenários testados do código atual: `DuplicateAndAutomaticIdsNeverReplaceLivingEntities`, `StackRegistryCanCreateAndAddComponents` e o teste existente `RemovingLastComponentKeepsEntityAlive` passam. | Cinco testes cobrem colisão de IDs inclusive sem Transform, operações sobre entidade ausente sem criar mapas, reposição e iteração sem duplicatas, uso na stack e remoção com liberação de componente/nome. | Não se afirma que `validate_invariants()` detecte toda corrupção dos mapas públicos. O problema de ownership já registrado em A1 não foi corrigido; o fixture remove explicitamente as entidades ao terminar. |
| Transform / hierarquia | CONFIRMADO: contratos exercitados por cinco novos testes aprovados. | Composição de translação/rotação/escala, conversão mundial/local, reparent/detach preservando posição mundial, rejeição de ciclos/pais ausentes e de matriz singular, round-trip TRS. | Sem nova decisão sobre shear ou decomposição com múltiplos eixos. Serialização do pai é responsabilidade da persistência de cena, não do round-trip local TRS. |
| Física Bullet | CONFIRMADO: 13 novos casos aprovados (incluindo seis parâmetros de formas). | Setup idempotente, escala absoluta, gravidade, adição/reposição/remoção de corpos, Transform estático com simulação pausada, raycast mais próximo/ponto/normal/miss, queda e contato com chão, seis formas, material/massa, entradas inválidas, forças/velocidade/travas. | Passos fixos no mundo Bullet real, sem sleeps. O teste de queda valida sincronização de posição de volta ao Transform; não cobre precisão da rotação física hierárquica nem todas as combinações de formas. |
| Ciclo Lua | CONFIRMADO: dois novos testes aprovados. | Anexar em execução inicializa uma vez; remoção interrompe updates; dois scripts mantêm estados independentes; reposição inicializa o novo e interrompe o anterior. | Usa Lua 5.4.6 neste host; Lua 5.3 não executado. Erros/logs/API/FPP permanecem cobertos pelas suítes existentes. |
| Demais bases | PARCIAL: suites existentes de projeto/cena, recuperação transacional, câmera/render, assets, editor e BGUI passaram na execução completa. | Matriz documenta os arquivos que exercitam cada base. Não foram duplicados os testes existentes. | Matriz de contratos não equivale a 100% de cobertura de linhas ou combinações. As demais hipóteses do diagnóstico não foram reavaliadas individualmente nesta fase. |

Validação em diretório novo, com os comandos derivados do CMake e do helper da CI:

```sh
cmake -S . -B /tmp/bengine-foundations-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build /tmp/bengine-foundations-20261029 --config Release --parallel 2
env -u DISPLAY -u WAYLAND_DISPLAY /tmp/bengine-foundations-20261029/tests/engine_foundations_tests
ctest --test-dir /tmp/bengine-foundations-20261029 --output-on-failure -C Release
```

Build novo de editor/runtime concluído; **25/25 casos novos passaram sem display**
e **184/184 entradas CTest passaram**, sem skips no `LastTest.log`, incluindo a
cobertura OpenGL existente. Logs: `/tmp/bengine-foundations-configure.log`,
`/tmp/bengine-foundations-build.log`, `/tmp/bengine-foundations-headless.log`,
`/tmp/bengine-foundations-ctest.log`. O CMake emitiu aviso de política CMP0135;
nenhum warning/error do compilador foi encontrado no log do build.
Como no helper da CI, o gerador único não recebeu `CMAKE_BUILD_TYPE`;
`--config Release` não comprova execução otimizada Release nesse gerador.
Windows, sanitizadores, pacote relocável, outros drivers e revisão interativa
não foram validados. Alterações locais anteriores foram preservadas e ficam fora
do commit desta fase; a execução completa reflete o estado atual compartilhado.


## Files view — mover e excluir arquivos (2026-10-29)

Continuação da importação por arrasto: exclusão definitiva somente após confirmação,
conforme escolha explícita do usuário. Escopo limitado ao navegador de arquivos;
nenhuma reavaliação das demais hipóteses de estabilização.

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Arrastar arquivos para pastas | CONFIRMADO: `AnyFileCanBeMovedByDroppingItOnAFolder` falhou antes da implementação: o arquivo de texto permanecia na origem. | Arrasto de qualquer tipo de arquivo para pastas da árvore lateral ou da lista; importação externa também aceita a pasta sob o cursor. Testes de movimento, importação direcionada, colisões, destino externo e bloqueio durante execução. | Não move diretórios inteiros. Colisões recusadas; operações restritas ao projeto. |
| Excluir com confirmação | CONFIRMADO: `DeletingAFileRequiresAnExplicitConfirmationAndSupportsCancel` falhou antes: não havia botão de exclusão. | Selecionar arquivo mostra “Excluir arquivo”; modal permite cancelar ou “Excluir definitivamente”. Teste verifica cancelamento, arquivo presente enquanto não confirmado e remoção somente após confirmar. | Arquivo do projeto e configuração oculta protegidos. Exclusão permanente não remove entidades nem corrige referências ausentes; o aviso informa esse efeito e o descarte de alterações não salvas. Salvar novamente uma cena ainda carregada pode recriar seu arquivo. |
| Documentos e referências após movimento | CONFIRMADO pelos testes `MovingAnOpenDirtyDocumentPreservesItsBufferAndSavesToTheNewPath`, `MovingScriptAndTextureUpdatesLiveAndSavedScenePaths`, `MovingTheMainSceneUpdatesProjectAndMarker`. | Documento aberto conserva buffer e passa a salvar no destino; atualização dos caminhos registrados de scripts, modelos e texturas na cena ativa e nos JSONs de cena/projeto, incluindo marcador de cena principal. | Caminhos literais em código Lua e formatos externos, como MTL/glTF, não são reescritos ao mover uma dependência isolada. Histórico de edições anterior ao movimento pode restaurar caminhos antigos; movimentos/exclusões não são ações de Undo/Redo da cena. |
| Dependências e falhas de gravação | CONFIRMADO pelos testes `MovingAModelCarriesItsDependenciesAndCanMoveBack` e `FailedReferenceStagingLeavesTheFileAndBlockingDirectoryUntouched`. | Modelos carregam cópias das dependências para o destino preservando referências relativas; originais ficam disponíveis para outros modelos. Dependência idêntica pode ser reutilizada; conteúdo diferente é recusado. Referências são preparadas antes de renomear; falhas tentam restaurar alterações. | Não há garantia transacional contra encerramento abrupto/queda de energia nem se a própria restauração falhar. Pastas vazias criadas para dependências podem permanecer após erro. |

Validação desta fase: **build limpo de editor/runtime concluído; 185/185 entradas CTest passaram**, sem skips, incluindo os testes OpenGL. Os **20/20 testes do fluxo de arquivos também passaram sem display** (`env -u DISPLAY -u WAYLAND_DISPLAY /tmp/cpp-bengine-files-management-20261029/tests/editor_asset_workflow_tests`).
Comandos derivados do CMake/CI:

```sh
cmake -S . -B /tmp/cpp-bengine-files-management-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build /tmp/cpp-bengine-files-management-20261029 --config Release --parallel 1
ctest --test-dir /tmp/cpp-bengine-files-management-20261029 --output-on-failure -C Release
```

Logs: `/tmp/bengine-file-management-clean.log`, `/tmp/bengine-file-management-tests.log` e `/tmp/bengine-file-management-headless.log`. Como no helper da CI, o gerador único não recebeu `CMAKE_BUILD_TYPE`; `--config Release` não comprova execução otimizada Release. Não houve warning/error do compilador no log; permanece o aviso CMake de política CMP0135.
Windows, outros gerenciadores de arquivos e revisão manual interativa ainda não validados.
As limitações de formato de imagem e hover externo registradas na fase anterior permanecem.
Alterações locais anteriores e o trabalho concorrente de cobertura das bases foram preservados.

## Texturas com separadores Windows e referências salvas (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Barras invertidas em texturas de material | CONFIRMADO: `MaterialTexturePathTest.NormalizesWindowsSeparatorsAndParentDirectoriesWithoutGpu` falhou antes da correção com barras simples e duplicadas no Linux. | `material::set_texture` converte separadores e normaliza segmentos `..`; preserva ID e caminho vazio. O teste passou sem DISPLAY/WAYLAND_DISPLAY. | Convenção de caminhos de assets: barras invertidas representam separadores. Não procura arquivos por nome nem altera diretórios arbitrariamente. Windows e revisão visual ainda não validados. |
| Caminho do projeto local `New Project` | CONFIRMADO: cena referenciava `assets/Texture` enquanto os sete arquivos existem em `assets/models/Texture`; o MTL referencia `Texture` ao lado de `trees9.obj`. | Sete referências da cena local corrigidas para `assets/models/Texture/…`; existência verificada após leitura do JSON. | Projeto local não versionado, fora do commit. Não há evidência de que `../Texture` válido falhe: reprodução independente importou esse caso corretamente. A origem histórica das referências erradas não foi reproduzida. |

Validação completa: build limpo de editor/runtime e **200/200 testes CTest passaram** em `/tmp/bengine-texture-clean-20261029`, configurado com BUILD_TESTING, BUILD_RUNTIME e BUILD_EDITOR ligados, conforme CMake/CI. O teste novo também passou explicitamente sem DISPLAY/WAYLAND_DISPLAY. Nenhum skip foi encontrado no LastTest.log. Windows e abertura manual do modelo no editor não foram validados; a suíte inclui testes OpenGL, mas isso não comprova a aparência deste modelo. O estado compartilhado inclui alterações anteriores fora deste commit. Como na CI, `--config Release` sem CMAKE_BUILD_TYPE não comprova otimização Release no gerador único. Logs: `/tmp/bengine-texture-clean-configure.log`, `/tmp/bengine-texture-clean-build.log`, `/tmp/bengine-texture-clean-tests.log`.

## Persistência, memória e proteção de arquivos — testes (2026-10-29)

Fase restrita a testes e documentação: `tests/persistence_integrity_tests.cpp`
integra o executável existente `editor_persistence_transaction_tests`. Nenhuma
mudança de produção ou API. As demais alterações locais foram preservadas.

| Item | Status / evidência | Testes adicionados | Limites / decisões pendentes |
|---|---|---|---|
| Persistência e corrupção de entrada / P0-b, P0-c | NÃO CONFIRMADOS nos novos cenários: falhas mantêm os mesmos componentes/mapas, nomes, entidades vivas, ocultas/bloqueadas e gravidade. | Todos os prefixos anteriores ao fechamento do JSON de projeto e cena; nove mutações de versão/esquema/vetores/IDs/pais/duplicatas; cena referenciada truncada. Round-trip repetido de nomes Unicode com aspas, quebras, tabulação e barra, e valores TRS. | Não reavalia cada hipótese histórica nem cobre toda entrada arbitrária. JSON sintaticamente válido com alteração de valores legítimos não é detectado como corrupção por estes testes. |
| Proteção da gravação / P0-a | NÃO CONFIRMADO nos novos cenários: bytes anteriores de projeto/cena e arquivo alheio são preservados quando a gravação é recusada. | Temporário bloqueado por diretório limpa somente temporários próprios; nova tentativa funciona; hard link no destino e symlink no temporário não alteram o arquivo vinculado. | Testes de links podem fazer skip se o sistema não os permitir; neste host ambos executaram. Não simula disco cheio, falha física de armazenamento ou queda de energia. O protocolo existente não usa fsync. |
| Journal corrompido e recuperação | CONFIRMADO: implementação valida o registro completo antes de restaurar; os testes verificam essa proteção. | Segunda entrada inválida não permite restaurar a primeira; sete formatos de journal inválido mantêm projeto/cena e o próprio journal byte a byte para diagnóstico. | Não há checksum para autenticar preimagens válidas adulteradas. Testes anteriores de interrupção do processo, rollback e recuperação repetida continuam no mesmo executável. |
| Liberação de componentes ao recarregar | CONFIRMADO nos cenários testados por `weak_ptr` e contagem de referências. | 25 loads sucessivos liberam Transform, estado/componente Lua e corpo Bullet substituídos, preservando a câmera; 50 loads inválidos não retêm referências adicionais aos componentes ativos; componente removido permanece utilizável enquanto um proprietário externo o mantém e expira após soltá-lo. | Não mede RSS nem todos os recursos internos/GPU; contagem de referências dos componentes ativos não prova ausência de alocações perdidas no staging de erros tardios. |
| Encerramento explícito do ECS | CONFIRMADO: remover a entidade libera Lua/Bullet e permite expirar o registro. | `ExplicitSceneShutdownReleasesRegistryLuaAndBulletComponents` verifica componentes e ECS com `weak_ptr`. | PARCIAL quanto a vazamentos globais: o ciclo de ownership A1 já documentado continua pendente; este teste exige remoção explícita, sem afirmar que soltar apenas o registro libere uma cena populada. ASan/LSan/UBSan não executados nesta fase. |

Validação dirigida: **14/14 testes novos sem display**, executados com
`env -u DISPLAY -u WAYLAND_DISPLAY` e filtro `PersistenceIntegrity.*`.
O executável completo de transações também passou: **32/32 testes sem display**,
incluindo os testes preexistentes de interrupção real do processo.
Logs: `/tmp/bengine-integrity-directed-build.log`,
`/tmp/bengine-integrity-directed-tests.log`,
`/tmp/bengine-integrity-transactions-headless.log`.

Validação final: build novo de editor/runtime concluído e **199/199 entradas CTest
aprovadas**, sem skips no `LastTest.log`, incluindo os testes OpenGL existentes.
Comandos derivados do CMake e do helper da CI:

```sh
cmake -S . -B /tmp/bengine-integrity-clean-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build /tmp/bengine-integrity-clean-20261029 --config Release --parallel 2
ctest --test-dir /tmp/bengine-integrity-clean-20261029 --output-on-failure -C Release
```

Logs finais: `/tmp/bengine-integrity-clean-configure.log`,
`/tmp/bengine-integrity-clean-build.log`, `/tmp/bengine-integrity-clean-tests.log`.
Nenhum warning/error de compilador encontrado no log; permanece o aviso CMake
CMP0135. Como na CI, `CMAKE_BUILD_TYPE` não foi definido: `--config Release` não
comprova otimização Release com gerador único. Windows, sanitizadores, falha real
de disco/energia e liberação de recursos GPU não foram validados. A suíte completa
reflete o estado compartilhado atual, incluindo alterações locais de outras tarefas;
o commit desta fase contém somente testes/configuração/documentação próprios.

## JPEG no Linux — BGUI, BEngine e Files view (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Leitura de JPG/JPEG | CONFIRMADO: o carregador não Windows chamava somente libpng; `ImageTest.DecodesJpegRgbGrayscaleAndCmykToRgba` falhou com JPEG real antes da correção (`/tmp/bengine-jpeg-red.log`). | BGUI identifica JPEG pela assinatura e usa libjpeg para converter RGB, tons de cinza, CMYK e JPEG progressivo para o mesmo buffer RGBA utilizado por PNG/WIC. Tratamento de erro mantém a aplicação viva; recursos são liberados inclusive na saída de erro da biblioteca C. Fixtures reais verificam dimensões, cores e alfa opaco; arquivo inválido deve lançar exceção. | API preservada. Não aplica orientação EXIF nem perfis ICC; a conversão CMYK é simples. PNG mantém o caminho anterior; Windows mantém WIC. |
| Integração BEngine e Arquivos | CONFIRMADO: `commons::texture::bind` e a prévia do painel usam o carregador da BGUI, mas a classificação Linux do painel aceitava somente PNG. O teste de integração falhou em `.JPG`/`.JPEG` antes da correção (`/tmp/bengine-jpeg-editor-red.log`). | Teste de importar/copiar JPEG, abrir a prévia e aplicar ao objeto alvo sem GPU. Testes gráficos verificam imagem JPEG renderizada na BGUI e pixels da textura enviada ao OpenGL pelo BEngine. | O comportamento sem objeto sob o cursor permanece sem aplicação de textura. O painel agora aceita `.jpg`/`.jpeg` também no Linux, sem distinguir maiúsculas; teste cobre encaminhamento do arquivo arrastado e uma imagem real na janela de prévia. |
| Dependências | CONFIRMADO: cabeçalhos libjpeg inicialmente ausentes; `libjpeg-dev`/`libjpeg-turbo8-dev` estão instalados na validação final. | CMake exige `JPEG::JPEG` somente fora do Windows. CI Linux instala `libjpeg-dev` e `libpng-dev`; README da BGUI documenta os pacotes. Nos testes iniciais, o pacote Ubuntu `libjpeg-turbo8-dev` 2.1.5 foi extraído localmente; o build final usa a instalação disponível no sistema. | Para builds Linux são necessários `libpng-dev` e `libjpeg-dev`; não há novos pacotes exigidos no Windows. |

Validação JPEG concluída: **editor/runtime compilados em diretório novo e 206/206 entradas CTest disponíveis passaram**, incluindo OpenGL. Os testes de leitura JPEG da BGUI e de importação/prévia/arrasto/aplicação do editor também passaram sem display. Configuração derivada do CMake/CI:

```sh
cmake -S . -B /home/d4nl/.cache/bengine-validation/jpeg-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build /home/d4nl/.cache/bengine-validation/jpeg-20261029 --config Release --parallel 1
ctest --test-dir /home/d4nl/.cache/bengine-validation/jpeg-20261029 --output-on-failure -C Release -E material_uv
```

Logs finais persistentes: `/home/d4nl/.cache/bengine-validation/jpeg-build.log` e `/home/d4nl/.cache/bengine-validation/jpeg-tests.log`. Os logs temporários anteriores não persistiram após a reinicialização do ambiente.
Windows, outros drivers e arrasto manual a partir de gerenciador externo não foram validados.
Como no helper da CI, `CMAKE_BUILD_TYPE` não foi definido; `--config Release` não implica
otimização Release no gerador único. Alterações locais anteriores foram preservadas.

## Cliques bloqueados pelo aviso de arraste (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Camada de arraste intercepta cliques mesmo oculta | CONFIRMADO: `asset_drop_overlay` permanece habilitado e ocupa toda a janela; `update_inputs` trata elementos com `receives_input(false)` como bloqueadores. Antes da correção, os dois testes dirigidos falharam: botões não recebiam cliques e o campo não ganhava foco (`/tmp/bengine-overlay-red.log`). | BGUI adiciona `set_input_passthrough`, opt-in que exclui o elemento e seus descendentes do teste de ponteiro sem interromper atualização ou desenho. Somente o aviso de arraste ativa a opção. A API existente e o bloqueio padrão são preservados. `editor_overlay_input_tests` cobre aviso oculto, arrasto nativo, arrasto interno, retorno ao estado oculto, foco/digitação, bloqueadores comuns e modais. | Os quatro testes de input passaram. O teste OpenGL `WindowFileDropTest.DropHintDarkensTheFramebufferAndDisappearsWithoutLeavingAnOverlay` também passou, preservando o aviso visual. Toque físico e interação manual em Windows não foram validados. Nenhum item P0/P1/P2 foi alterado nesta correção. |

Validação em diretório novo: `bash scripts/build.sh /tmp/bengine-input-fix-build --test`.
A compilação geral foi bloqueada por alteração local anterior em
`tests/editor_asset_workflow_tests.cpp:554`: `ecs::add<renderer>(target.id, ...)`
passa um ID onde a API exige `entity&`. O arquivo foi preservado.
Todos os demais alvos foram compilados, incluindo editor, runtime, BGUI e o novo
`editor_overlay_input_tests`. CTest executou 185 entradas, excluindo somente
`editor_asset_workflow_tests_NOT_BUILT`: **184 passaram e uma falhou**.
A falha é `GL3Tests`, especificamente o teste local de JPEG
`GL3Test.DecodedJpegRendersAsAnImage` (pixel preto e `GL_INVALID_VALUE`);
esse teste chama diretamente o desenho de imagem, sem executar o caminho de input.
Não se afirma suíte geral verde nem build completo sem erros.
Logs: `/tmp/bengine-overlay-red.log`, `/tmp/bengine-overlay-green.log`,
`/tmp/bengine-input-fix-build.log`, `/tmp/bengine-input-remaining-build.log`
e `/tmp/bengine-input-tests.log`. Como no helper da CI, `CMAKE_BUILD_TYPE`
não foi definido; `--config Release` não implica otimização em gerador único.

A construção geral encontrou o alvo concorrente e ainda não concluído `material_uv_tests`,
que inclui `loaders/mtl_uv.hpp` ausente. Esse trabalho foi preservado; somente seu placeholder
`material_uv_tests_NOT_BUILT` ficou excluído do CTest acima. Não se afirma que o alvo
alheio compila. Logs adicionais: `jpeg-editor-headless.log`, `jpeg-bgui-headless.log`,
`jpeg-engine-gpu.log` e `jpeg-gl3-final.log` no mesmo diretório de validação.

O novo teste gráfico inicialmente expôs cache estático de shaders preservado entre dois
contextos OpenGL independentes (`GL_INVALID_VALUE` no segundo contexto). Esse ciclo de vida
não foi alterado nesta fase: o caso existente agora testa PNG e JPEG no mesmo contexto,
limpando o framebuffer entre as imagens. Revisão de múltiplos contextos permanece pendente.

## Aviso de arrasto — impedir ativação sem arquivo (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Falso positivo nativo X11 | CONFIRMADO na regra isolada: `XdndSelection` retida + botão pressionado + coordenadas internas bastavam, sem oferta de arquivo. A reprodução extraída da regra anterior falhou em `hover-red.log`. | Sessão explícita iniciada por `XdndEnter` com oferta `text/uri-list`; `XdndLeave`, `XdndDrop`, liberação do botão e mudança do dono da seleção encerram a sessão. Dois testes sem GPU cobrem seleção antiga, arrasto comum/texto, saída, retorno, soltura e troca de origem. | Testa a política separada do servidor X. Arrasto real por gerenciador externo não foi exercitado manualmente; oferta URI é a identificação de arquivo do protocolo usado pelo GLFW. O payload continua validado na importação. |
| Estado interno e cursor fora da janela | CONFIRMADO: `InternalHintRequiresHeldButtonAndCursorInsideWindow` falhou antes da correção (`hover-internal-red.log`). | O aviso exige cursor dentro da área da janela e limpa o estado interno quando o botão esquerdo não está mais pressionado. O teste também verifica que um clique posterior não revive o aviso. | A saída durante um arrasto ainda pressionado só oculta o aviso; voltar com o mesmo arrasto permite mostrá-lo novamente. |
| Integração com eventos GLFW | CONFIRMADO por build e teste nativo `NativeEventObserverIsChainedAndRestoredWhenTheLoopEndsOrThrows`. | Hook opcional no backend GLFW observa a fila antes de o GLFW consumi-la. O observador X11 mantém todos os eventos na fila, incluindo o drop original. O loop do BEngine preserva/restaura o observador anterior tanto ao finalizar como em exceção. | API existente preservada; novo hook aditivo. Windows e Wayland não foram executados nesta fase. A detecção OLE no Windows foi mantida. |

Validação sem display: **7/7 testes de sessão/overlay passaram** em
`editor_overlay_input_tests`. Teste nativo de encadeamento/restauração também passou
com janela real oculta. **Validação final concluída em diretório novo: editor/runtime e todos os alvos compilaram; 217/217 entradas CTest passaram, sem exclusões e sem skips, incluindo OpenGL.**

Logs persistentes em `/home/d4nl/.cache/bengine-validation/`: `hover-red.log`,
`hover-internal-red.log`, `hover-focused-tests.log`, `hover-native-tests.log`,
`hover-build.log`, `hover-final-build.log` e `hover-tests-final.log`. O ambiente interrompeu uma compilação;
o build foi retomado em diretório persistente. O código-fonte local do GoogleTest,
já obtido na fase JPEG, foi reutilizado para evitar nova obtenção pela rede.

O alvo ausente `material_uv_tests` da fase JPEG já não consta da configuração atual.
A execução final usou a construção geral e o CTest **sem filtros**. O teste antigo de
transparência passou a simular o botão pressionado durante um arrasto real, mantendo
a nova regressão separada para o estado sem botão. Configuração/build/CTest:

```sh
cmake -S . -B /home/d4nl/.cache/bengine-validation/hover-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/.cache/bengine-validation/jpeg-20261029/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/hover-20261029 --config Release --parallel 1
ctest --test-dir /home/d4nl/.cache/bengine-validation/hover-20261029 --output-on-failure -C Release
```

Como no helper da CI, `CMAKE_BUILD_TYPE` não foi definido; não se afirma otimização
Release no gerador único. Alterações de outros trabalhos foram preservadas.

## Física configurável e gravidade do player FPP (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Tipos de corpo | CONFIRMADO: `CharacterFallsLandsAndAcceptsImpulsesWithoutTumbling` passa, com passos Bullet fixos sem GPU. | Adicionado seletor Rigid Body / Character Body / Static Body. Character é dinâmico, recebe gravidade/impulsos e trava rotação física, conforme decisão do usuário. Mudança de tipo atualiza massa, trava angular e registro no mundo. | Não implementa controlador cinemático, degraus ou salto. Construtor e métodos públicos anteriores preservados. |
| Colisor configurável | CONFIRMADO: `ColliderShapeSizeAndOffsetCanChangeInWorld`, `TriggerReportsContactsWithoutBlockingAndDisabledColliderIsExcluded` e `ColliderConfigurationRoundTripsAndRejectsInvalidValues`. | Forma, escala local, deslocamento, ativação, trigger, atrito e restituição no componente existente. Forma composta aplica deslocamento sem deslocar Transform/câmera. Forma/escala/offset editados atualizam AABB e pares de colisão. Desativação também exclui consultas por raio. | Trigger fornece contatos nativos Bullet sem resposta; callbacks Lua não foram adicionados. Esferas exigem escala uniforme; plano infinito deve usar Static Body. Malhas fora do conjunto aprovado. |
| Inspector e persistência | CONFIRMADO: `InspectorExposesBodyTypesAndColliderControlsWithHistory` e `RoundTripsCharacterAndColliderSettings` passam sem GPU. | Seletores, controles numéricos e toggles com histórico; novos campos persistem no JSON e arquivos antigos continuam aceitos. | Não houve inspeção visual/manual dos novos controles. O teste exercita seleção, toggles e desfazer/refazer. |
| Movimento FPP | CONFIRMADO: `LuaApi.FppScriptsMoveLookAndCollectUsingTheNewApi` e `FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`. | Player usa cápsula Character Body de massa 80, rotação inicial vertical e colisor deslocado acima dos pés. Script controla velocidade horizontal e preserva velocidade Y. Rotação visual controlada por script não é sobrescrita pela simulação. | Abertura breve do runtime com display carregou o projeto e registrou setup do player sem erros de script. Movimento WASD e interação visual não foram exercitados manualmente. |
| Limites físicos do cenário | CONFIRMADO por reprodução com configuração original do chão: o teste do spawn falha após quatro segundos de simulação (base da cápsula em Y=-78,3), pois o jogador em Z=17 fica fora do colisor antigo, de semiextensão 10. Com a configuração nova, o teste passa. | Escala do colisor do chão acompanha a malha de extensão 40; deslocamento/espessura posicionam a superfície em Y=0. Paredes ajustadas às dimensões das malhas. Nenhuma malha ou Transform de chão/parede foi alterado nesta tarefa. | Outros modelos adicionados localmente pelo usuário não receberam colisores automaticamente. |
| Validação completa | CONFIRMADO no Linux: build novo completo terminou com código 0; CTest final passou em **217/217** (13,66 s). Todos os **30 testes de fundamentos** passaram. | Editor, runtime e todos os alvos de teste compilados em diretório novo. Testes novos, teste Lua atualizado e suíte completa executados. | A primeira rodada teve 216/217: o teste do aviso de arrasto havia sido compilado antes de uma atualização paralela de seu preparo de entrada. Após reconstruir esse alvo com a fonte atual, a suíte completa passou. Nenhum arquivo do aviso de arrasto foi alterado nesta tarefa. Windows não compilado/executado. |

Comandos descobertos no CMake/workflow e usados nesta fase:

```sh
cmake -S . -B /home/d4nl/.cache/bengine-validation/fpp-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/.cache/bengine-validation/jpeg-20261029/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/fpp-20261029 --parallel 2
ctest --test-dir /home/d4nl/.cache/bengine-validation/fpp-20261029 --output-on-failure -C Release
```

Como no helper da CI, `CMAKE_BUILD_TYPE` não foi definido. O GoogleTest local foi
reutilizado. Logs em `/home/d4nl/.cache/bengine-validation/fpp-20261029/validation/`:
`fpp-clean-build.log`, `fpp-final-build.log`, `fpp-final-tests.log`, `fpp-foundations.log`,
`fpp-floor-original-red.log`, `fpp-overlay-baseline-test.log` e `fpp-runtime.log`.
Uso e semântica dos campos em `docs/PHYSICS.md`. Os itens P0/P1/P2 do diagnóstico
não foram reabertos nesta implementação. Demais alterações locais foram preservadas.

## Corrupção de memória ao iniciar o runtime — artefatos incompatíveis (2026-10-29)

| Item | Status / evidência | Ação / testes | Limites / revisão |
|---|---|---|---|
| Crash na abertura | CONFIRMADO: `out/apps/runtime_app --project projects/fpp_demo/project.bengine` interrompeu em `SIGSEGV` dentro de `unlink_chunk`/`malloc`, durante importação da parede pelo Assimp. Backtrace em `fpp-crash-gdb.log`. | Reproduzido antes de reconstruir os executáveis; comparado com build separado e coerente, que abre o mesmo projeto. | A mensagem do usuário foi `double free or corruption (out)`. A reprodução confirma corrupção do heap; não confirmou uma chamada duplicada de `delete`. |
| Incompatibilidade de tamanho do componente | CONFIRMADO nos objetos de `out`: informação DWARF de `project_persistence_system.cpp.o` indicava `_Sp_counted_ptr_inplace<commons::physics,...>` de 136 bytes (16 de controle + 120 do componente), enquanto o construtor em `physics.cpp.o` usa componente de 128 bytes. A gravação excedia o espaço reservado. | Reconstrução completa com `cmake --build out --clean-first --parallel 2`, sem alterar fontes durante a compilação. Após reconstruir, o bloco compartilhado tem 144 bytes (16 + 128); evidência em `fpp-fixed-layout.log`. | O objeto antigo foi produzido durante mudanças na estrutura do componente. O build incremental considerava seu timestamp recente e não o recompilava. A validação anterior em diretório separado não detectava a inconsistência de `out`. Não foi necessária mudança na API nem na propriedade dos ponteiros Bullet. |
| Runtime reconstruído | CONFIRMADO: o runtime em `out/apps` abriu o FPP, registrou setup do player e permaneceu vivo por 10 s com `MALLOC_CHECK_=3`; encerrado pelo teste com SIGTERM. | Também criado build separado com `ENABLE_SANITIZERS=ON` e Debug: runtime permaneceu ativo por 10 s sem diagnóstico ASan/UBSan; 30/30 testes de fundamentos passaram sob ASan/UBSan. | Detecção de vazamentos desativada (`ASAN_OPTIONS=detect_leaks=0`); estes resultados cobrem acessos inválidos/UB, sem atestar ausência de vazamentos. WASD e o botão Play do editor não foram exercitados manualmente. Editor já aberto deve ser reiniciado para carregar o binário novo. |
| Verificação final de out | CONFIRMADO: build completo terminou com código 0; CTest em `out` passou em **217/217**, em 11,73 s. | Editor, runtime e alvos de teste reconstruídos do zero no diretório usado pelo usuário. Nenhum teste-fonte novo: a falha dependia da mistura de objetos incompatíveis, sem reprodução em builds coerentes do código atual. A regressão foi validada pela abertura que falhava antes e passou depois da reconstrução. | Windows não foi executado. Alterações locais de outros trabalhos preservadas. |

Comandos desta fase (opções de sanitização verificadas no CMake; build/teste
alinhados com o helper usado pelo workflow):

```sh
cmake --build out --clean-first --parallel 2
ctest --test-dir out --output-on-failure -C Release
cmake -S . -B /home/d4nl/.cache/bengine-validation/fpp-memory-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DENABLE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/.cache/bengine-validation/jpeg-20261029/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/fpp-memory-20261029 --target runtime_app engine_foundations_tests --parallel 2
```

O cache existente de `out` usa Debug. Logs e evidências preservados em
`/home/d4nl/.cache/bengine-validation/fpp-memory-20261029/validation/`:
`fpp-crash-gdb.log`, `fpp-crash-layout.log`, `fpp-fixed-layout.log`,
`fpp-out-clean-build.log`, `fpp-out-tests.log`, `fpp-out-startup-fixed.log`,
`fpp-out-startup-result.json`, `fpp-asan-runtime.log` e `fpp-asan-foundations.log`.

## Player FPP não sobe a rampa — rotação do colisor (2026-10-29)

| Item | Status / evidência | Correção e testes | Limites / revisão |
|---|---|---|---|
| Colisor diverge da rampa visível | CONFIRMADO: `CompoundRampColliderMatchesRenderedEulerRotation` falhou antes da correção. A rampa atual usa Euler `(62,297; -50,769; 50,344)` e escala `(6,253; 1; 9,099)`. A componente Y da normal visual é 0,8247, mas a normal física anterior era -0,2313. | `Transform` monta `Rx * Ry * Rz`; a conversão Bullet usava `setEulerZYX`, que monta a ordem inversa. `physics_system.cpp` agora compõe os três quaternions na mesma ordem do Transform. O teste compara os três eixos físicos com os eixos visuais, incluindo escala não uniforme. | Rampa/cenário/script do usuário não foram modificados. A correção vale para corpos com rotações combinadas; hierarquia física não foi ampliada nesta fase. |
| Subida da cápsula | CONFIRMADO: `FppCharacterClimbsTheRenderedRampSurface` falhou antes da correção; a cápsula caía por não encontrar o colisor na superfície desenhada. | Teste Bullet sem GPU usa pose/dimensões da rampa e cápsula Character Body do FPP (massa 80, atrito zero, escala 0,7/0,6/0,7 e offset Y=0,9). Aplica velocidade horizontal de 2,5 m/s preservando Y, como o script. Após a correção, verifica ganho de altura maior que 0,8 m e avanço maior que 2 m na direção de subida. | Teste usa passos fixos e superfície isolada. Aproximação manual com WASD, diferentes entradas da rampa e transições em degraus não foram exercitadas. Não foi adicionado controlador cinemático nem novo limite de inclinação. |
| Rotação física devolvida ao Transform | CONFIRMADO por `RigidBodyOrientationRoundTripsCombinedEulerAngles`. | A conversão de volta também extrai Euler na ordem XYZ, preservando a orientação visual dos Rigid Bodies. O teste compara matrizes antes/depois de sincronizar com simulação pausada. API pública preservada. | Ângulos Euler podem ter representações numéricas diferentes para a mesma orientação; a comparação por matriz verifica o resultado geométrico. |
| Validação | CONFIRMADO: build completo terminou com código 0; **220/220 testes** passaram em `out` (6,05 s). Runtime abriu o FPP e permaneceu ativo por 10 s, sendo encerrado pela validação. | Três regressões adicionadas; **33/33** testes de fundamentos passaram após a correção, também sob ASan/UBSan (`detect_leaks=0`, sem validar vazamentos). Build completo de `out` executado com limpeza prévia. | Windows e subida manual no editor não foram validados. Alterações locais de outros trabalhos preservadas. |

Build/teste seguem os alvos do CMake e o fluxo de CI já inspecionados:

```sh
cmake --build out --clean-first --parallel 2
ctest --test-dir out --output-on-failure -C Release
```

Evidências em `/home/d4nl/.cache/bengine-validation/fpp-ramp-20261029/`:
`fpp-ramp-red2.log` (dois testes falhando antes da correção),
`fpp-ramp-green.log`, `fpp-ramp-asan-tests.log`, `fpp-ramp-clean-build.log`,
`fpp-ramp-tests.log` e `fpp-ramp-runtime.log`. O cache de `out` permanece Debug;
nenhuma alteração de arquitetura de renderização ou do ECS foi necessária.


## Resolução de sombra no Inspector e shadow acne (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Resolução ausente no Inspector | CONFIRMADO: `InspectorShadowControlsSupportUndoAndRedo` falhou por não encontrar `Shadow Resolution`; renderizador usava constante 2048. | Campo numérico na luz direcional, padrão 2048, intervalo 128–8192; persistência JSON com compatibilidade para arquivos antigos e validação de tipo/intervalo. Inspector preserva desfazer/refazer. Renderizador redimensiona a textura ao mudar o valor e respeita `GL_MAX_TEXTURE_SIZE`. Testes cobrem edição/histórico/limites e persistência sem GPU, além da troca de resolução com leitura de pixels. | O limite efetivo da GPU pode ser menor que o valor salvo. Não foi adicionada nova API Lua nesta tarefa. |
| Acne em superfície plana com iluminação rasante | CONFIRMADO: teste OpenGL com plano isolado, direção `(1,-0.2,0)` e bias `0.000001` encontrou 4692 pixels indevidamente escurecidos; limite esperado menor que 10. | Shader acrescenta ao bias a variação de profundidade geométrica no raio do filtro PCF, incluindo arredondamento da amostra mais próxima. Derivadas calculadas antes da saída por limites do mapa. Teste verifica ausência de acne e preservação da sombra de outro objeto em 128, 512 e 2048. | Compensação conservadora pode afastar sombras de contato, sobretudo em baixa resolução e ângulos extremos. Degenerações da projeção mantêm o bias configurado. Não houve avaliação visual manual do projeto FPP nem execução em Windows/outros drivers. |

Validação em diretório novo concluída: editor, runtime e todos os alvos de teste
compilaram com código 0. **231/232 entradas CTest passaram**, sem exclusões.
Todos os testes de sombras passaram, incluindo OpenGL; edição/histórico e
persistência também passaram com `DISPLAY` removido.
A única falha foi `Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`:
base da cápsula em Y=-24,64136 e velocidade Y=-20,36254. O executável antigo de
`out/tests/engine_foundations_tests`, sem reconstrução nesta tarefa, reproduz os
mesmos valores com a cena local atual (`fpp-existing-failure.log`). A cena do usuário
e o sistema de física foram preservados; não se afirma suíte inteira verde.

Comandos derivados do CMake/workflow:

```sh
cmake -S . -B /home/d4nl/.cache/bengine-validation/shadow-settings-20261029 -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/repos/cpp-bengine/out/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/shadow-settings-20261029 --parallel 2
ctest --test-dir /home/d4nl/.cache/bengine-validation/shadow-settings-20261029 --output-on-failure -C Release
```

Após ajustar somente a posição do objeto no cenário de regressão rasante para sua
sombra permanecer dentro do plano visível, `editor_render_tests` foi recompilado
antes do CTest completo. Como no helper de CI, `CMAKE_BUILD_TYPE` não foi definido.
Logs em `/home/d4nl/.cache/bengine-validation/shadow-settings-20261029/validation/`:
`shadow-clean-build.log`, `shadow-all-tests.log`, `shadow-focused.log`,
`shadow-inspector-headless.log`, `shadow-persistence-headless.log`,
`shadow-inspector-red.log`, `shadow-acne-red.log` e `fpp-existing-failure.log`.
O diretório `out` não foi reconstruído; os executáveis novos estão em `apps/`
no diretório de validação acima. O editor já aberto usa o binário antigo.
Os itens P0/P1/P2 do diagnóstico original não foram alterados nesta tarefa.
Alterações locais anteriores foram preservadas, incluindo implementação prévia de
sombras e mudanças já preparadas para commit nos mesmos arquivos.

O usuário autorizou incluir a implementação anterior de sombras no mesmo commit.
O commit reúne somente sombras, resolução, correção de acne, testes e documentação;
mudanças locais de física, colisores, cena FPP, interface e submódulo ficam de fora.

## Colisor alinhado à malha/Transform e visualização F3 (2026-10-29)

| Item | Status / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Ajuste automático aos limites da malha | CONFIRMADO: `DefaultColliderFitsModelBoundsAndInheritsEntityScale` falhou antes da implementação; as seis faces não coincidiam com os limites esperados. | Novos componentes começam com `fit_to_model=true`. Limites locais dos vértices fornecem centro/dimensões; o Transform mundial aplica posição, rotação e escala. `AutoFitColliderFollowsParentTransformAndLocalOffsets` cobre pai e escala não uniforme. Sem modelo, a forma unitária continua disponível. | Caixa ajusta AABB local; esfera/cápsula/cilindro/cone são aproximações primitivas. Esfera usa raio conservador sob escala não uniforme. Não há ajuste contínuo a deformações de animação nem representação exata de cisalhamento. |
| Configuração como offsets locais | CONFIRMADO: `FitAndRotationOffsetsPersistAndLegacySizesArePreserved`, `RotationOffsetIsLocalAndSingularTransformDoesNotCorruptTheShape` e persistência de projeto. | Posição e rotação locais usam zero como padrão; escala é multiplicador unitário sobre a referência. Inspector ganhou `Fit to Model` e `Rotation Offset`, com histórico. Configuração persiste; cenas sem o novo campo preservam tamanhos antigos e podem ativar ajuste pelo Inspector. Escalas singulares recebem dimensão mínima para manter Bullet válido. | Não foi alterado o Transform visual. Atualizações de hierarquia usam decomposição afim; escalas/rotações que geram cisalhamento são aproximadas. |
| Contornos do colisor | CONFIRMADO: `ColliderPreviewSupportsEveryShapeAndShowsDisabledAndTriggerColors` sem GPU e `ColliderOverlayDrawsWireframeAndRestoresOpenGLState` com OpenGL real. | Geometria vem do `debugDrawObject` do Bullet e da mesma sincronização usada pela física; aparece mesmo sem simulação. Reutiliza renderizador de linhas dos gizmos, sem mudar arquitetura de renderização. Verde: sólido ativo; azul: trigger; cinza: desativado. Teste gráfico leu pixels verdes e verificou restauração de framebuffer, depth test e blending. | Teste gráfico usa janela oculta; aparência não foi avaliada manualmente nesta tarefa. |
| F3 e menu View | CONFIRMADO: `F3AndViewMenuShareColliderVisibilityWithoutRepeatToggling`, teste do Inspector e `NativeF3KeyIsDeliveredToTheSharedInputState`. | View → `Show/Hide Colliders    F3` e F3 compartilham estado no editor; tecla mantida/repeat não alterna repetidamente. Runtime também possui F3 e usa o mesmo desenho. BGUI adiciona enum sem renumerar valores existentes e mapeia GLFW_KEY_F3. Commits locais: BGUI `07aa11f`, backend `f7032c0`. | Visualização é temporária, sem modificar projeto/preferências. Runtime compilado e abertura exercitada; F3 manual no runtime e Windows não foram exercitados. Publicação dos commits de submódulos não foi executada. |
| Esfera entra na rampa / player atravessa lateral | CONFIRMADO por reprodução com os vértices reais de cube.obj/sphere.obj e configuração observada da rampa (`fit_to_model=false`). `SphereContactsTheVisibleRampSurfaceWithoutSinkingIntoTheModel` mediu contato 0,500001 unidade dentro da superfície; `CharacterIsStoppedByTheVisibleRampSide` mediu cápsula já 2,776 unidades dentro da lateral visível. | Com ajuste ligado, os dois testes passam: esfera toca superfície da malha (tolerância 0,02) e cápsula é bloqueada na lateral, com velocidade normal zerada. Foi ligado `fit_to_model` da entidade 3 no arquivo local da cena. A tarefa não alterou sua posição/rotação/escala, conforme escolha do usuário. | A cena contém alterações prévias e edições em andamento do usuário; foi preservada como dado local não incluído no commit de código. Não foi movida a rampa para o chão. Passagem por baixo permanece possível quando há espaço. |
| Validação completa | PARCIAL no cenário editado: build completo finalizou; **231/232 testes passaram**. Todos os novos testes passaram. | Build começou com limpeza de out e foi retomado sequencialmente após falhas transitórias de descoberta de teste (permissão) e link. Sem modificar código para contornar essas falhas. A única falha final é `FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`; esse mesmo teste passa com uma cópia isolada da cena de HEAD, sem alterar a cena de trabalho. | O teste antigo exige repouso no chão horizontal em Y=0; a cena local agora tem piso inclinado (rotação X=-170,413°, em vez de 180°). Não se corrigiu configuração de piso/spawn nem se afirmou que o spawn esteja abaixo do piso. Demais alterações locais, inclusive de outros trabalhos, preservadas. |

Comandos de build/teste continuam os alvos do CMake e fluxo do workflow:

```sh
cmake --build out --clean-first --parallel 2
cmake --build out --parallel 1
ctest --test-dir out --output-on-failure -C Release
```

Logs em `/home/d4nl/.cache/bengine-validation/collider-20261029/`:
`collider-fit-red.log`, `collider-final-foundations.log`, `collider-contact-red.log`,
`collider-contact-green.log`, `collider-clean-build.log`, `collider-retry-build.log`,
`collider-full-tests.log`, `collider-reference-test.log`, `collider-render.log`,
`collider-native-f3.log` e `collider-runtime.log`. Reprodução negativa de contato usa
fixture idêntica com ajuste automático desligado; testes permanentes usam ajuste ligado.
Leitura dos vértices OBJ nos testes não usa o carregador de modelos que cria buffers GL.
O cache de out usa Debug. Detalhes de uso em `docs/PHYSICS.md`. Itens P0/P1/P2 do
inventário anterior não foram reabertos.

### Validação do conteúdo separado para commit

Após autorização do usuário, os arquivos mistos foram preparados parcialmente,
sem modificar suas alterações locais restantes. Uma cópia exata da árvore
preparada foi extraída em
`/home/d4nl/.cache/bengine-validation/shadow-commit-20261029/source`, usando as
revisões registradas dos submódulos (inclusive o backend da BGUI).
Editor, runtime e todos os alvos compilaram do zero: **221/221 entradas CTest
passaram**, sem exclusões (6,87 s), incluindo o teste gráfico de sombras e o teste
de spawn do FPP com a cena versionada. Essa cópia valida o conjunto de sombras
isolado sobre a base `a82ddf3d`, independentemente das demais alterações locais.
Durante a validação, outro trabalho registrou as mudanças de física no commit
`ede55e4f`, que passou a ser o pai do commit de sombras. A rodada anterior de
231/232 continua sendo o resultado do workspace completo com essas alterações
de física e com a cena local presentes; não se atribui o resultado 221/221 à
árvore integrada posterior.

```sh
cmake -S /home/d4nl/.cache/bengine-validation/shadow-commit-20261029/source -B /home/d4nl/.cache/bengine-validation/shadow-commit-20261029/build -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/repos/cpp-bengine/out/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/shadow-commit-20261029/build --parallel 2
ctest --test-dir /home/d4nl/.cache/bengine-validation/shadow-commit-20261029/build --output-on-failure -C Release
```

Logs: `config.log`, `build.log`, `tests.log` e `shadow-focused.log` em
`/home/d4nl/.cache/bengine-validation/shadow-commit-20261029/`.
Somente esta nota de documentação foi acrescentada após a validação da árvore.

## Texturas do cubo e controles de mapeamento (2026-10-29)

| Item | Status e evidência | Mudança / testes | Limites e decisões |
|---|---|---|---|
| Textura aparece como um único pixel | CONFIRMADO: `PrimitiveTextureTest.CubeTrianglesHaveNonDegenerateUvsOnEveryFace` falhou antes da correção em `HasTextureCoords(0) == false`, importando o OBJ com os mesmos flags do carregador. As faces de `cube.obj` omitiam os índices `vt`; `model::process_mesh` preenchia UVs com zero. | Faces agora referenciam os UVs existentes. Teste sem GPU exige área UV não nula em cada triângulo. Posições, índices de normais e API preservados. | A cena local usa `cube.obj` também para a superfície achatada. Não foi criado outro modelo de plano nem modificado o projeto do usuário. |
| Offset / repetição / WorldTexture | IMPLEMENTADO conforme confirmação do usuário: projeção em coordenadas do mundo, mantendo densidade ao escalar o objeto. | Inspector oferece `UV Offset X/Y`, `Texture Repeat X/Y`, `WorldTexture`, com undo/redo. Valores persistem por material e aplicam a todos os mapas PBR. Sem campos novos: offset zero, repetição um, WorldTexture desligado. Testes de Inspector sem GPU, serialização sem GPU, round-trip com modelo real e leitura de pixels OpenGL. | Em UV normal, repetição significa repetições por UV unitária; em WorldTexture, repetições por unidade do mundo. Zero é permitido e valores negativos espelham. Projeção planar escolhe eixo dominante da face geométrica; superfícies curvas podem mostrar transições entre eixos (não é mistura triplanar). |
| Repetição e estado compartilhado | CONFIRMADO: o backend BGUI carrega imagens com `GL_CLAMP_TO_EDGE`. | Desenho da malha usa `GL_REPEAT` temporariamente e restaura os modos anteriores, preservando imagens do editor. Teste gráfico verifica repetição além de UV=1, restauração e ausência de vazamento de parâmetros entre materiais. | Implementação usa as funções OpenGL já disponíveis no projeto, sem alterar a arquitetura ou o backend BGUI. |

Validação final: editor, runtime e todos os testes compilados após limpeza de
`out`. A tentativa paralela encontrou dependência ausente de `libbgui_core.a`;
a retomada serial encontrou uma ocorrência de `Text file busy` na descoberta
do teste. Reconfiguração e nova retomada serial concluíram sem mudança de código
para contornar esses erros. **235/236 entradas CTest passaram**, sem testes
ignorados. Todos os testes de texturas/Inspector/persistência passaram, inclusive
leitura de pixels com OpenGL real em janela oculta.

A única falha foi `Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`,
que lê a cena local modificada antes desta tarefa. O mesmo teste passou com uma
cópia isolada do arquivo de teste, cena de HEAD e bibliotecas recém-compiladas;
nenhum arquivo do projeto local foi substituído. Não foram alterados posição,
rotação, escala, materiais ou outros dados da cena do usuário.
Não foram feitos teste manual do viewport nem build no Windows. A projeção em
superfícies curvas não foi validada visualmente; a escolha entre eixos é discreta.
Nenhuma quebra de API ou decisão pendente nesta fase. O inventário P0/P1/P2
anterior não foi reaberto.

Comandos usados (descobertos no CMake e workflow):

```sh
cmake --build out --clean-first --parallel 2
cmake -S . -B out -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON
cmake --build out --parallel 1
ctest --test-dir out --output-on-failure -C Release
```

Logs: `/home/d4nl/.cache/bengine-validation/uv-20261029/`:
`clean-build.log`, `config.log`, `rebuild.log`, `retry-build.log`, `tests.log`,
`inspector.log`, `persistence.log`, `render.log`, `reference-build.log` e
`reference-test.log`. A cópia de referência foi compilada com as mesmas flags e
bibliotecas do alvo CMake `engine_foundations_tests`; somente a localização do
arquivo de teste e a cena lida por `__FILE__` mudam.

## Gizmo de direção do Scene Editor (2026-10-30)

| Item | Status e evidência | Mudança / testes | Limites e decisões |
|---|---|---|---|
| Indicador de direção no canto superior direito | CONFIRMADO: `scene_view::build` não adicionava indicador de orientação. Implementado em `apps/editor/tools/direction_gizmo.hpp` e integrado em `apps/editor/panels/scene_view.cpp`. | Eixos X/Y/Z coloridos e identificados, projetados pela rotação da view, com profundidade ordenada, tamanho adaptado à escala da interface e viewport. Não captura input. A prévia de câmera fica abaixo do gizmo. Três testes sem GPU verificam orientação/independência de translação, posição/escala/viewports pequenas e desenho sem captura de input. | Indicador visual, sem comandos de alinhamento por clique. API pública preservada; inventário P0/P1/P2 não reaberto. |

Validação: `editor_app` compilou com a integração. Os **27 testes** do arquivo
`editor_interaction_tests.cpp`, incluindo os três novos, passaram sem GPU.
O executável de validação `/tmp/bengine-direction-interaction-tests` foi ligado
com o objeto desse arquivo e as mesmas bibliotecas do `link.txt` do CMake,
excluindo somente o objeto do novo arquivo concorrente
`editor_lua_properties_tests.cpp`, cuja compilação falhou por passar entidade
`const` a `ecs::add(entity&)` (linhas 43 e 101 na execução). Nenhum código desse
outro trabalho foi alterado para contornar a falha.

Comandos de build descobertos no CMake, workflow e `scripts/build.sh`:
`cmake --build out --clean-first --parallel 2`, seguido de
`cmake --build out --parallel 2` e
`cmake --build out --target editor_app editor_interaction_tests --parallel 2`.
A tentativa de build limpo não concluiu: `ar` encontrou `lua.cpp.o` ausente,
em ambiente com outras compilações simultâneas usando `out`. A retomada compilou
o editor, mas o alvo integrado de interação encontrou os erros concorrentes
acima. Portanto **build limpo completo e suíte CTest completa não validados**.
Também não houve inspeção visual/manual da viewport nem build no Windows.
Logs: `/tmp/bengine-direction-clean-build.log`,
`/tmp/bengine-direction-rebuild.log`, `/tmp/bengine-direction-final-build.log`
e `/tmp/bengine-direction-tests.log`.

## Fase: importador 3D configurável — 2026-10-30

Esta fase implementa a solicitação de importação de modelos. As hipóteses P0/P1/P2
anteriores não foram reabertas. Alterações preexistentes em scripts, cenas,
Inspector e testes Lua foram preservadas e não integram o commit desta fase.

| Item | Diagnóstico / evidência | Mudança e testes | Limites / revisão |
|---|---|---|---|
| Janela e diagnóstico | CONFIRMADO: o fluxo anterior de `editor_app.cpp` só mostrava uma mensagem em falhas; `files_view.hpp` não tinha ação para inspecionar o modelo. | `model_import_dialog.hpp`, ação Mostrar importador 3D e abertura em erros de drop/Renderer. Testes `ImportDialogTest` exercitam confirmação, erro, nova tentativa e escala inválida. | Janela usa os widgets e o bloqueio modal da BGUI; não houve comparação visual manual com Godot. |
| Estrutura / seleção | CONFIRMADO: `model::process_node` percorria todas as meshes sem seleção persistida. | Seleção de subárvores por caminho ordinal e de meshes por índice; opções de UVs, normais, união de vértices e escala. `ModelImport.FiltersMeshesAndNodeSubtrees` e `AppliesHierarchyAndScaleWithoutGpu`. | Nova ordem dos dados na origem exige revisar as seleções. A hierarquia é mantida no modelo, sem gerar uma entidade ECS para cada node. |
| Skins / skeleton / animations | CONFIRMADO: o carregador anterior detectava ossos/animações apenas para evitar `PreTransformVertices`; não avaliava poses ou pesos. | Avaliação de hierarquia, interpolação de posição/escala/quaternion, pesos e offsets na CPU; opções separadas de skeleton, skins e clipes. Testes de interpolação/loop, exclusão e pesos/offsets. | Não inclui morph targets, retargeting, blending, editor de keyframes ou conversão de câmeras/luzes; estes limites aparecem no importador. Rigs com nomes duplicados e grandes cenas não receberam validação específica nem benchmark. |
| Persistência / reimportação | CONFIRMADO: não havia arquivo de opções do modelo. | Arquivo `.import.json` com versão 1, escrita temporária e substituição; acompanha dependências. Editor/runtime usam o mesmo leitor. Reimportação atualiza Renderers ativos e prepara as novas malhas antes de substituir as anteriores. Testes de round-trip, corrupção, cópia e preservação do modelo em erro. | Substituição cobre falhas de processo/escrita comuns; não garante durabilidade em queda de energia. Materiais editados no Renderer são reconstruídos na reimportação. |
| Formatos / texturas embutidas | PARCIAL: a lista anterior do navegador não incluía `.blend`; o carregador tratava referências embutidas como caminhos externos. | Reconhecimento dos formatos do Assimp e extensões comuns mesmo sem decodificador; GLB com imagem embutida usa o decodificador BGUI, incluindo pixels sem compressão. Teste gráfico gera/exporta GLB, carrega textura e animação, verifica upload e erro de reimportação. | Arquivos reais FBX e `.blend` de diferentes versões não foram validados; suporte depende do Assimp. Decodificação comprimida usa um arquivo transitório e o cache de imagens BGUI. |

Validação final: editor e runtime compilados a partir de uma pasta nova.
**11/11 testes novos passaram**, sem skips, incluindo upload OpenGL da textura
embutida e avaliação de animação em um GLB gerado pelo teste. A execução CTest
teve **202/206 entradas aprovadas**: três entradas `*_NOT_BUILT` correspondem
aos testes Lua bloqueados na compilação; a outra falha foi
`Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`.
Esse teste também foi compilado isoladamente com as mesmas flags/bibliotecas,
alterando apenas a localização da fonte e usando uma cópia da cena de HEAD:
**passou**. A cena local modificada não foi substituída. Não houve teste manual
da aparência da janela, build Windows, teste de arquivos reais FBX/Blender,
benchmark ou validação de rigs com nomes duplicados. Assim, o build integral
do diretório atual continua bloqueado pelos testes Lua preexistentes.

Comandos encontrados em `CMakeLists.txt`, `tests/CMakeLists.txt` e
`.github/workflows/build_and_test.yml`:

```sh
bash scripts/build.sh /tmp/bengine-import-clean --test
cmake --build /tmp/bengine-import-clean --parallel 2 -- -k
/tmp/bengine-import-clean/tests/model_import_tests
ctest --test-dir /tmp/bengine-import-clean --output-on-failure -C Release
```

A primeira configuração partiu de uma pasta inexistente. O build integral
identificou erros nas alterações Lua que já estavam no diretório: entidades
`const` passadas para `ecs::add(entity&)` em `editor_persistence_tests.cpp`,
`editor_lua_properties_tests.cpp` e `editor_script_tests.cpp`. Esses arquivos
não foram corrigidos nesta fase. Logs preservados em `/home/d4nl/.cache/bengine-validation/importer-20261030/`
(`build.log`, `tests.log`, `ctest.log` e `reference-test.log`).
Detalhes de uso e limites: [MODEL_IMPORT.md](MODEL_IMPORT.md).


## Correção pontual: sombreamento do cubo (2026-10-30)

| Item | Status e evidência | Mudança / teste | Limites e riscos |
|---|---|---|---|
| Normais do cubo | CONFIRMADO: `commons/assets/models/cube.obj` referenciava as normais 1 a 6 nas seis faces, mas as normais estão agrupadas em blocos de quatro. Cinco faces usavam direções incorretas. `PrimitiveMeshTest.CubeNormalsMatchEveryFace` falhou antes da correção. | Referências corrigidas para 1, 5, 9, 13, 17 e 21. Teste sem GPU importa o OBJ com Assimp e verifica normal geométrica, direção externa e as normais dos três vértices de todos os 12 triângulos; passou após a correção. | API e shaders preservados. Aparência manual no editor não validada; modelos externos não são alterados. |

Validação: `bash scripts/build.sh /tmp/bengine-cube-clean --test` partiu de pasta nova;
editor, runtime e commons compilaram. Build integral bloqueado por erros preexistentes
nos testes Lua modificados (`editor_persistence_tests.cpp:668`,
`editor_lua_properties_tests.cpp:43`, `editor_script_tests.cpp:204`): entidades const
passadas a `ecs::add(entity&)`. `cmake --build /tmp/bengine-cube-clean --parallel 2 -- -k`
compilou os demais alvos. `ctest --test-dir /tmp/bengine-cube-clean --output-on-failure -C Release`:
203 de 207 entradas aprovadas, incluindo o novo teste e os testes gráficos de renderização;
três suítes NOT_BUILT pelos erros acima e falha em
`Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn` (não investigada nesta correção).
Os 13 testes de commons também passaram em `out/tests/commons_tests`.
Logs: `/tmp/bengine-cube-before.log`, `/tmp/bengine-cube-clean.log`,
`/tmp/bengine-cube-remaining.log`, `/tmp/bengine-cube-final-tests.log`.
Revisão pendente: aparência do cubo no editor e bloqueios da suíte integral.

## Propriedades Lua editáveis por entidade (2026-10-30)

Escopo autorizado: propriedades explícitas com tipo e padrão; valores por entidade
salvos na cena. Esta fase acrescenta um recurso e não reabre as hipóteses P0/P1/P2.
A API pública anterior foi preservada.

| Item | Status e evidência | Mudança / testes | Limites e decisões |
|---|---|---|---|
| Propriedades no Inspector | CONFIRMADO: o Inspector anterior expunha apenas o caminho do script. O teste `LuaPropertiesInspectorTest.InspectorLuaPropertiesSupportTypedFieldsAndUndo`, compilado com uma cópia do Inspector de HEAD anterior à mudança e a biblioteca atual, falhou em `speed == nullptr` (`baseline-test.log`). | Declarações `--@property` com JSON são lidas sem executar Lua. Campos de número, booleano, texto, cor RGBA e referência de imagem, com desfazer/refazer. Imagens aceitam caminho e drag-and-drop. Teste sem GPU cobre edição, transparência, validação, histórico, drop e proteção durante Play/entidade travada. | Declaração por comentário evita executar código durante edição. Identificadores únicos; erros informam arquivo e linha. A atualização das declarações ocorre ao reconstruir o Inspector ou iniciar a execução. |
| Valores por entidade/script e persistência | CONFIRMADO: a serialização anterior guardava apenas `directory` e caminhos em `scripts`. | Propriedades tipadas são salvas por script, preservando o formato anterior dos caminhos. Testes `EditorScriptTest.*Properties*`, `EditorScriptTest.LegacyScenesStillLoadAndChangedDeclarationTypesUseNewDefaults` e `ProjectPersistenceTest.LuaPropertyValuesAreSavedPerEntityAndPerScript` cobrem isolamento, round-trip, cenas antigas, dados inválidos sem substituição parcial e cenas salvas fora da raiz. | Nome/tipo mantidos preservam o valor da entidade; declaração nova ou tipo alterado usa o padrão; remoção elimina a propriedade. Os valores do script promovido ao remover o primeiro também são preservados. |
| Entrega ao Lua e imagens | CONFIRMADO: não havia tabela de propriedades na inicialização. | Tabela `properties` disponível antes do código de nível superior e de `setup()`. Cores usam `Color` da API existente. Referências de imagem são resolvidas na raiz do projeto e acompanham movimentações de arquivos, sem alterar propriedades de texto semelhantes. Testes cobrem início/update, múltiplos scripts, metadados inválidos e referências movidas. | Imagem é uma referência de caminho, sem carregar textura automaticamente. Alterações feitas pelo Lua na tabela não gravam a cena. Não há quebra da API de scripts existente. |

Uso e contrato completos: [LUA_PROPERTIES.md](LUA_PROPERTIES.md).

Validação final: compilação iniciada em pasta nova, incluindo editor, runtime e
todos os testes, concluída sem erros. **12/12 testes focados passaram** (oito testes
novos e quatro testes existentes de scripts). Suíte integral: **258/259 entradas
CTest aprovadas**, sem testes ignorados ou ausentes. A única falha foi
`Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`: lê somente
transform/physics da cena local modificada, sem executar Lua; os resultados
`minimum.y() = -24.641359329223633` e velocidade Y `-20.362541198730469` são idênticos
aos já registrados em `/tmp/bengine-cube-final-tests.log` antes desta conclusão.
Nenhum dado da cena foi alterado para contornar a falha.

Pasta de validação limpa e independente de `out`:
`/home/d4nl/.cache/bengine-validation/lua-properties-20261030/`.
Logs: `config.log`, `build.log`, `resume-build.log`, `baseline-compile.log`,
`baseline-link.log`, `baseline-test.log`, `focused-tests.log` e `tests.log`.
A primeira compilação foi interrompida após gerar `commons`; a retomada usou a mesma
pasta. Um erro de `const entity` nos testes novos foi corrigido antes da validação.
A árvore também contém trabalho paralelo de importador, gizmo e cubo, além da cena
local modificada; esses dados não foram editados por esta fase.

Comandos derivados do CMake e do workflow de CI:

```sh
cmake -S . -B /home/d4nl/.cache/bengine-validation/lua-properties-20261030/build -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/repos/cpp-bengine/out/_deps/googletest-src
cmake --build /home/d4nl/.cache/bengine-validation/lua-properties-20261030/build --parallel 2
ctest --test-dir /home/d4nl/.cache/bengine-validation/lua-properties-20261030/build --output-on-failure -C Release
```

Limites: não foi feito teste visual manual do Inspector nem build no Windows.
Os controles foram exercitados sem GPU; a suíte integral também executou os testes
OpenGL existentes com o display disponível. Referências de imagem não carregam
texturas automaticamente e mover um arquivo não reescreve os padrões no Lua.
Nenhuma decisão de design obrigatória ficou pendente; o contrato confirmado pelo
usuário está documentado. A falha física da cena local permanece fora desta fase.

## Skybox cubemap e atmosfera — 2026-10-30

Escopo solicitado: cubemap de seis imagens por câmera; atmosfera gradiente por
shader como padrão e substituta da skybox desabilitada. As hipóteses P0/P1/P2
não foram reabertas nesta implementação.

| Item | Status | Evidência / mudança |
|---|---|---|
| Fundo sem atmosfera | CONFIRMADO | `camera::drawFB` apenas limpava a cor e `render_system::update` não desenhava fundo. Adicionado passe de fundo antes dos objetos, com shader `background.vert/frag`. |
| Cubemap configurável | CONFIRMADO | `skybox` só expunha um ID OpenGL. Agora carrega seis faces quadradas de mesmo tamanho, com filtros lineares e clamp; Inspector da câmera aceita caminhos e arrastar imagens, com undo/redo. |
| Atmosfera inicial e fallback | CONFIRMADO | `m_use_skybox` inicia falso; cenas antigas sem o campo também ficam com atmosfera. Cubemap incompleto/inválido usa gradiente. Cores do horizonte (`ceu`) e zênite são editáveis. |
| Persistência | CONFIRMADO | `camera::serialize/analyze` gravam faces e zênite; validação de cena verifica tipos e tamanhos antes da aplicação. Caminhos relativos são resolvidos pela raiz do projeto no editor/runtime; imagens internas arrastadas são gravadas com caminho relativo. |

Ordem das faces: direita (+X), esquerda (-X), topo (+Y), base (-Y), frente (+Z),
fundo (-Z). O fundo acompanha a rotação da câmera e ignora a translação; não
escreve profundidade. Recursos de GPU são criados apenas durante renderização.
A API existente `skybox::draw`, `id_skybox` e `camera::m_use_skybox` foi mantida.

Testes adicionados em `tests/editor_render_tests.cpp`: round-trip sem GPU e cena
antiga; gradiente e fallback de imagem ausente; independência de translação e
preservação da profundidade; cubemap com imagem PNG e retorno à atmosfera.

Validação: em andamento; resultados e limites registrados abaixo antes do commit.

## Auditoria de custo das janelas BGUI — 2026-10-31

Escopo: procurar desperdícios, especialmente após destacar janelas de tabs,
sem alterar funcionalidade. Nenhum código de produção foi modificado.

| Item | Status | Evidência | Mudança / decisão |
|---|---|---|---|
| Alvos de encaixe ocultos invalidam o layout novamente | CONFIRMADO | `dock.cpp:767-777`: `mark_style_dirty()` incondicional nos cinco alvos e no preview; chamado no fim de `arrange()` (`:1485`). Teste `WindowAudit.AfterDetachIdleArrangeShouldKeepLayoutClean` falha: dock continua dirty após arrange sem interação. | Apenas diagnóstico; candidato a evitar invalidação quando o estado não muda. |
| Indicadores de tabs invalidam layout em repouso | CONFIRMADO | `dock.cpp:1102-1104`: visibilidade reatribuída e estilo invalidado em toda atualização. Teste `WindowAudit.IdleTabsShouldKeepLayoutClean` falha sem interação. | Apenas diagnóstico; candidato a invalidar somente na transição de estado. |
| Destacar uma tab reativa todas as antigas integrantes | CONFIRMADO (comportamento); PARCIAL (impacto de desempenho) | `dock.cpp:497-514` desfaz o grupo e habilita seus membros; teste `WindowAudit.DetachEnablesPreviouslyHiddenWindows` passa com três janelas. `layout::on_update()` atualiza filhos habilitados. | Preservado: manter as demais tabs agrupadas seria mudança funcional e exige decisão separada. Custo depende do conteúdo das janelas. |

Validação: `cmake --build out --target all_tests -j 2` concluído; CTest
`ElementTests`, `LinearTests`, `DockTests`, `DrawTests`, `ProfilingTests`: 5/5
aprovados. Diagnósticos temporários em `/tmp/bgui_window_audit.cpp`, compilados
contra as bibliotecas atuais: duas expectativas de layout limpo falham e um
teste de caracterização passa. Log: `/tmp/bgui_window_audit.log`.

Limites: build incremental do alvo BGUI; não houve build limpo integral,
medição de FPS/tempo de GPU, teste visual manual nem simulação completa do gesto
físico de destacar tab. O teste de pós-destaque usa `set_floating(true)` e o
fluxo real de sincronização/arrange. Nenhuma correção ou commit foi feito nesta
auditoria; alterações anteriores da árvore de trabalho foram preservadas.

## Otimização de invalidações do dock BGUI — 2026-10-31

Escopo: melhorias da auditoria de janelas, sem alterar API ou comportamento de
agrupamento, destaque, encaixe, seleção de tabs e redimensionamento.

| Item | Status / evidência | Correção e cobertura |
|---|---|---|
| Tabs em repouso invalidavam layout | CONFIRMADO: `DockTest.IdleTabsShouldKeepLayoutClean` falhou antes da correção. | Indicador só invalida estilo quando sua visibilidade muda; teste cobre três atualizações consecutivas. |
| Alvos de encaixe já ocultos invalidavam layout após destaque | CONFIRMADO: `DockTest.AfterDetachIdleArrangeShouldKeepLayoutClean` falhou antes da correção. | Alvos e preview só invalidam ao mudar a visibilidade para `false`; comparação considera `std::optional<bool>`. Teste cobre três passes após destaque. |
| Divisórias dependiam da invalidação contínua | CONFIRMADO durante validação: `DockTest.RightSideSplitResizesInDragDirection` falhou após retirar as invalidações em repouso. | Arraste de divisória solicita layout explicitamente. Suíte DockTests cobre divisórias e encaixe. |
| Reativação das antigas integrantes do grupo | CONFIRMADO; comportamento preservado. | `DockTest.DetachEnablesPreviouslyHiddenWindows` caracteriza o destaque em grupo de três janelas. Nenhuma mudança no desagrupamento. |

Os três testes adicionados estão em `external/cpp-bgui/tests/dock_tests.cpp`.
As alterações de produção ficam em `core/source/lay/dock.cpp` da BGUI.

Validação integrada: `cmake --build out --target all_tests -j 2` e
`ctest --test-dir out -R '^(DockTests|ElementTests|LinearTests|DrawTests|ProfilingTests)$' --output-on-failure`:
5/5 suítes aprovadas, incluindo os três testes novos.

A execução direta de `all_tests` em processo único resulta em 120/123 testes
aprovados. As três falhas de LinearTest (`WrapContentMeasuresMatchParentChildrenByIntrinsicSize`,
`MeasuresNestedWrapContentBeforeSizingItsParent`, `MeasuresContentInsideNonFlexLayouts`)
também ocorreram com `dock.cpp` original, recompilado e ligado aos mesmos objetos
(117/120 testes antigos aprovados). A suíte LinearTests isolada passa. Esse
problema preexistente de execução conjunta fica fora do escopo.

O primeiro build standalone com extensões GNU encontrou colisão preexistente da
macro `linux` em `icon_tests.cpp`. A configuração final usa
`CMAKE_CXX_EXTENSIONS=OFF`, como o projeto raiz; nenhum arquivo alheio foi alterado.

Build limpo BGUI em `/tmp/bgui-window-improvements-clean`, sem OpenGL/GLFW,
com exemplos desabilitados e GoogleTest local:

```sh
cmake -S external/cpp-bgui -B /tmp/bgui-window-improvements-clean -DCMAKE_CXX_EXTENSIONS=OFF -DBGUI_USE_OPENGL=OFF -DBGUI_USE_GLFW=OFF -DBGUI_BUILD_EXAMPLES=OFF -DBGUI_TESTS=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=/home/d4nl/repos/cpp-bengine/out/_deps/googletest-src
cmake --build /tmp/bgui-window-improvements-clean -j 2
ctest --test-dir /tmp/bgui-window-improvements-clean --output-on-failure
```

Resultado: build concluído e 18/18 suítes CTest aprovadas.
Limites: não houve build limpo integral da engine/editor, validação visual manual,
medição de FPS/GPU ou teste no Windows. A transição de destaque nos testes usa
`set_floating(true)`; os testes existentes cobrem arraste, encaixe e divisórias.
Não há mudança de design pendente nesta correção. A reativação das demais
janelas continua sendo comportamento existente, sem otimização funcional.

## Build de jogos Linux / Windows — 2026-10-31

Escopo: Linux x64 gera Linux nativo e Windows; Windows x64 gera Windows nativo
e Linux pela distribuição WSL2 padrão, conforme decisão explícita do usuário.
Android fica para outra fase. API pública e arquitetura de renderização preservadas.
As demais hipóteses P0/P1/P2 não foram reabertas.

| Item | Status / evidência | Mudança e testes | Limites / riscos |
|---|---|---|---|
| Linux nativo dependia de ferramentas cruzadas | CONFIRMADO: o teste nativo falhou no script original com saída 3, exigindo vcpkg mesmo com bibliotecas do sistema disponíveis. | `platform_build.sh` usa compilador/bibliotecas do host para Linux, sem baixar Zig/vcpkg. Teste simula comandos e verifica flags, assets e progresso; exportação real Linux concluída. | Pacote Linux continua dependendo das bibliotecas compartilhadas do sistema; não é AppImage nem pacote universal. Hosts/alvos desta fase são x64. |
| Windows nativo passava pelo WSL | CONFIRMADO: wrapper original exigia WSL incondicionalmente; o novo teste de Windows nativo falhou contra essa versão. | `platform_build.ps1` usa MSVC/Visual Studio 2022 e vcpkg para Windows, copia DLLs/assets e propaga erros. WSL2 fica exclusivo do alvo Linux. Testes PowerShell com comandos simulados cobrem ambos os caminhos, espaços e falha do CMake. | Compilação MSVC e execução WSL2 em host Windows real NÃO validadas neste ambiente Linux. CI ganhou empacotamento Windows nativo e build cruzado Linux → Windows; workflow remoto ainda não executado. |
| Opções de aplicações ignoradas | CONFIRMADO: configuração original com `BUILD_EDITOR=OFF` e PNG desabilitado falhou; `apps/CMakeLists.txt` criava os dois executáveis incondicionalmente. | Opções `BUILD_RUNTIME`/`BUILD_EDITOR` passam a controlar os alvos e dependências. Teste configura/compila runtime sem fontes do editor e configura ambos desligados sem fontes do runtime. | Editor e runtime com opções ligadas compilaram na validação integral. Nenhuma quebra de API. |
| Pasta de saída alterava a raiz da engine | CONFIRMADO: `EditorMenuTest.BuildOutputDirectoryDoesNotReplaceEngineSourceDirectory` falhou com `editor_ui.cpp` original ligado isoladamente, procurando `/tmp/scripts/platform_build.sh`; passou com a correção. | Raiz fornecida pelo CMake, independente da pasta de build/saída. Teste do menu troca diretório e impede o processo antes de iniciar compilador. `build_process` valida o script do host; dois testes sem GPU verificam script ausente, argumentos, log, progresso e retorno. | Editor ainda precisa do checkout da engine para compilar runtimes. Não houve inspeção manual da janela de build. |
| Bloqueios reais na compilação Windows | CONFIRMADO: build cruzado falhou no GLM experimental em `transform_gizmo.cpp` e depois em tipos OpenGL/macros Windows em `file_drop_bridge.cpp`. Logs `bengine-cross.log` e `bengine-cross-retry.log`. | Apenas ordem dos includes corrigida: configuração GLM existente antes de GTX; GLAD/BGUI antes dos headers nativos Windows. Runtime Windows compilado e empacotado depois das correções. | Dependências/ferramentas cruzadas reutilizaram o cache existente; download/bootstrap a frio não foi exercitado. Smoke de argumentos via Wine não valida gameplay ou OpenGL Windows. |

Validação final:

- Build integral iniciado em pasta nova `/tmp/bengine-platform-clean`, incluindo editor, runtime e testes, concluído após reconfiguração/retomada. A primeira tentativa usou flags anteriores à definição da raiz; a versão final compilou sem erros.
- **264/266 entradas CTest aprovadas**, sem skips. Passaram os sete casos novos: quatro testes Python de scripts/CMake, dois testes de processo e um teste do menu (quatro entradas CTest).
- Falhas fora do escopo: `EditorRenderTest.CubemapOverridesAtmosphereAndDisableRestoresIt` (cores de faces, `editor_render_tests.cpp:565`) e `Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn` (altura -24.641359 e velocidade Y -20.362541). O código/teste de cubemap e a cena FPP já estavam modificados localmente antes desta fase; não foram corrigidos ou substituídos aqui. A causa da falha de cubemap não foi investigada nesta fase.
- Exportações reais Linux nativa e Linux → Windows concluídas com progresso 100, assets copiados e formatos ELF x64 / PE32+ x64 verificados. `ldd` do runtime Linux não mostrou bibliotecas ausentes. Ambos responderam ao argumento inválido com a mensagem de uso e saída 2; Windows executado via Wine com prefixo isolado.
- Regressões contra scripts/CMake originais: três falhas esperadas; caminho WSL já existente continuou passando. Regressão isolada do menu também falhou na versão original e passou na atual.
- Alterações locais de skybox, cena, assets e submódulo BGUI foram preservadas e não integram o commit desta fase. A validação usa a árvore de trabalho atual, incluindo essas alterações preexistentes/concomitantes.

Comandos derivados do CMake, workflow e helpers existentes:

```sh
cmake -S . -B /tmp/bengine-platform-clean -DBUILD_TESTING=ON -DBUILD_RUNTIME=ON -DBUILD_EDITOR=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/out/_deps/googletest-src"
cmake --build /tmp/bengine-platform-clean --parallel 2
ctest --test-dir /tmp/bengine-platform-clean --output-on-failure -C Release
python3 tests/platform_build_tests.py
sh scripts/platform_build.sh linux "$PWD" /tmp/bengine-native-export /tmp/bengine-native-export/progress
sh scripts/platform_build.sh windows "$PWD" /tmp/bengine-cross-validate /tmp/bengine-cross-progress
```

Logs finais: `/tmp/bengine-platform-build-final.log`,
`/tmp/bengine-platform-final-check.log`, `/tmp/bengine-platform-ctest-final.log`,
`/tmp/bengine-platform-script-tests.log`, `/tmp/bengine-platform-script-baseline.log`,
`/tmp/bengine-platform-ui-baseline-test.log`, `/tmp/bengine-platform-menu-test.log`,
`/tmp/bengine-native-export.log`, `/tmp/bengine-cross-verified.log` e
`/tmp/bengine-platform-wine.log`. Pacotes em
`/tmp/bengine-native-export/platform-builds/linux/apps` e
`/tmp/bengine-cross-validate/platform-builds/windows/apps`.
Os helpers continuam gerando runtime e assets da engine; arquivos/assets do jogo
acompanham separadamente e são abertos com `--project`, conforme README.

## Exportação do jogo completo e saída limpa — 2026-10-31

Esta fase corrige o escopo da fase anterior: o usuário pediu o jogo com seus
assets, e não somente um runtime. Decisão confirmada: referências externas devem
ser importadas para o projeto antes da exportação; assets internos da engine são
incluídos automaticamente. Android permanece fora do escopo.

| Item | Status / evidência | Mudança / testes | Limites / revisão |
|---|---|---|---|
| Projeto ausente no build anterior | CONFIRMADO: `PlatformBuildTests.test_game_export_has_clean_output_and_project_assets` falhou antes da implementação, pois não existia `game.json` nem projeto no output (`/tmp/bengine-game-baseline.log`). | `Build > Export Game` salva o projeto/cena atuais e envia o projeto ao exportador. Host tool C++20 independente de OpenGL prepara cenas, scripts, assets, sidecars e manifesto; nome do executável deriva da pasta do projeto. | A exportação inclui recursos não usados também; não há redução de assets nem arquivo binário `.pak`. Dados ficam em pastas portáteis. Projetos precisam estar válidos para carregar no runtime. |
| Output misturava CMake/logs/binários | CONFIRMADO: helpers anteriores compilavam dentro do diretório escolhido e `build_process` gravava log/progresso nele. | Cache separado para ferramenta host, compilação, snapshot, logs e progresso. Output contém somente `linux/` e/ou `windows/`, com executável, `game.json`, `game/`, `assets/`, bibliotecas Linux em `lib/` ou DLLs Windows. Layout, preview, VCS, fontes de desenvolvimento e resíduos de build são excluídos. Testes verificam ausência de CMakeCache, objetos, logs e PDBs. | Diretório com arquivos alheios é recusado, nunca apagado. Exportação anterior identificada pelo manifesto é substituída por staging/rename com backup após completar o novo pacote; não se promete durabilidade contra queda de energia. |
| Caminhos e dependências de assets | CONFIRMADO: a cena local contém caminhos absolutos de modelos internos da engine. | Caminhos reconhecidos de cena/projeto, modelos, mapas PBR, scripts, imagens de propriedades e skybox tornam-se relativos. Preserva texto comum. Inclui OBJ/MTL, buffers/imagens glTF e `.import.json`, valida sidecars, normaliza barras Windows e URIs com espaços. Assets externos/ausentes e links para fora falham antes de publicar; links de diretório exigem importação. | Literais/dinâmica em Lua não são inferidos: código deve usar referências portáteis. Formatos binários de modelos devem ter sido importados com dependências relativas/embutidas; não há reescrita geral de FBX/BLEND. O fallback de primitivos já existente no carregador foi preservado para cenas salvas em outro checkout. |
| Resources BGUI faltavam no pacote | CONFIRMADO: rastreamento inicial abriu tema/fonte em `external/cpp-bgui/assets` do checkout; teste de cópia falhou para `3270NerdFont-Regular.ttf` e `dark.theme` (`bengine-game-gui-baseline.log`). | Recursos BGUI agora acompanham os assets da engine. Rastreamento final do jogo movido não acessou qualquer caminho do checkout nem o projeto fonte de referência. | Não houve alteração no submódulo. Inicialização atual da BGUI ainda exige uma fonte do sistema antes de selecionar a fonte empacotada; o prefixo Wine isolado precisou receber uma fonte para o smoke. |
| Abrir o jogo sem argumentos | CONFIRMADO: runtime anterior sem argumentos criava o cubo de demonstração. | Bootstrap encontra `game.json` ao lado do executável, valida formato/versão/entrada, muda cwd para o pacote e carrega a cena inicial. Mantidos `--project`, `--script-log` e demo de desenvolvimento quando não há manifesto. Testes sem GPU cobrem mudança de pasta, relocação, manifesto inválido, arquivo ausente e escape da entrada. | Execução Linux real comprovou carregamento dos scripts do pacote; smoke Windows via Wine chegou à interface, mas não comprova gameplay nem comportamento no Windows nativo. |
| Dependências e plataformas | PARCIAL: builds reais Linux nativo e Linux → Windows concluídos em workspaces inicialmente novos; ELF/PE x64 e arquivos finais verificados. | Linux inclui dependências compartilhadas transitivas com `DT_RPATH=$ORIGIN/lib`; `ldd` resolveu bibliotecas empacotadas. Windows copia somente executável/DLLs, sem PDBs ou arquivos do compilador. CI passou a exportar o jogo FPP com assets. | Linux continua exigindo glibc compatível, stack gráfica/driver do host e fonte do sistema. MSVC/WSL2 em Windows real e workflow remoto não executados aqui; caminhos PowerShell/WSL foram testados com comandos simulados. Cache vcpkg/toolchains foi reutilizado, sem teste de bootstrap/download a frio. |

API preservada: a assinatura anterior de `build_process::start` continua chamando
os helpers internos `compile_runtime.*`; a nova sobrecarga recebe o projeto e é
usada pelo editor para exportar o jogo. `platform_build.*` agora são os pontos de
entrada da exportação e exigem o arquivo do projeto. Nenhuma mudança de ECS,
renderização, multithreading ou API Lua; hipóteses P0/P1/P2 não reabertas.

Validação final: editor, runtime e testes compilaram. Os builds de jogo Linux e
Windows recompilaram engine/BGUI/runtime em diretórios inicialmente novos.
**280/282 entradas CTest aprovadas**, incluindo os 15 testes `GameExportTest`,
três testes de processo, teste do menu e seis casos Python (uma entrada CTest).
As duas falhas são as mesmas registradas antes desta fase:
`EditorRenderTest.CubemapOverridesAtmosphereAndDisableRestoresIt` e
`Foundations.FppScenePlayerLandsOnConfiguredArenaAtItsSavedSpawn`.

A cena FPP local também falhou no Renderer da entidade 5, tanto no pacote quanto
abrindo diretamente o projeto fonte com o mesmo executável. Causa não investigada
nesta fase; cena, materiais, scripts e modelos locais não foram corrigidos ou
substituídos. Para isolar o exportador, foi extraída uma cópia do projeto FPP
versionado de HEAD em `/tmp/bengine-game-reference/projects/fpp_demo`.
Essa cópia foi exportada para Linux e Windows; o jogo Linux movido para outra
pasta iniciou sem argumentos, executou os scripts FPP e foi encerrado pelo teste
após cinco segundos. `strace` não registrou acesso ao checkout inteiro nem ao
projeto fonte. No Wine isolado, após adicionar uma fonte ao prefixo, o pacote
Windows carregou a cena e inicializou UI; interrompido após dez segundos.
Não houve avaliação manual de gameplay/qualidade visual nem execução MSVC/WSL2
real. As modificações preexistentes de skybox e FPP permanecem fora deste commit.

Comandos derivados de CMake/CI/helpers e usados nesta fase:

```sh
cmake --build /tmp/bengine-platform-clean --target editor_app runtime_app editor_menu_tests game_export_tests build_process_tests --parallel 2
ctest --test-dir /tmp/bengine-platform-clean -C Release --output-on-failure
sh scripts/platform_build.sh linux "$PWD" /tmp/bengine-game-export /tmp/bengine-game-native-work/progress "$PWD/projects/fpp_demo/project.bengine" /tmp/bengine-game-native-work
sh scripts/platform_build.sh windows "$PWD" /tmp/bengine-game-export /tmp/bengine-game-windows-work/progress "$PWD/projects/fpp_demo/project.bengine" /tmp/bengine-game-windows-work
```

Logs finais: `/tmp/bengine-game-final-build.log`,
`/tmp/bengine-game-ctest-final.log`, `/tmp/bengine-game-unit-final.log`,
`/tmp/bengine-game-current-linux-final.log`,
`/tmp/bengine-game-current-windows-final.log`,
`/tmp/bengine-game-source-load.log` e
`/tmp/bengine-game-reference-smoke/linux-final.log`, `linux-files-final.log`,
`windows-with-font.log`. Pacotes do projeto local: `/tmp/bengine-game-export`;
pacotes do exemplo versionado: `/tmp/bengine-game-reference-export`.
Uso, conteúdo e requisitos atualizados no README.

## 2026-10-31 — CI de release e compatibilidade Windows

A validação da primeira release expôs bloqueios antes encobertos pela seleção de
Lua 5.5: conversões ambíguas de resultados sol2 no MSVC, tags `class`/`struct`
divergentes na declaração antecipada de `ecs`, herança de `DefaultIOSystem`
(agora `final` no Assimp), e colisões com macros Windows em testes e na BGUI.
As chamadas Lua usam proteção explícita e extração tipada de erros; o tracker
Assimp delega ao IO padrão por composição. Nomes de escopos dos testes evitam
a macro `interface`, sem alterar seu comportamento.

Na BGUI, somente os nomes dos parâmetros da projeção ortográfica foram
alterados para `near_plane`/`far_plane`, mantendo sua assinatura. A reprodução
com macros vazias `near` e `far` falhou antes da correção e compilou após ela.
O novo teste `MatrixCompatibility.OrthographicAcceptsWindowsNearFarMacros`
passou executado isoladamente com GoogleTest. A suíte Linux anterior a estes
ajustes tinha 324 testes aprovados, incluindo OpenGL por software e execução
real do demo por snapshots temporários, com Play/Pause/Stop e reinício.
A recompilação completa e a rodada MSVC dos ajustes de compatibilidade são
registradas nos resultados do Actions; execução gráfica Windows depende da
disponibilidade de contexto OpenGL 3.3 no ambiente.

A recompilação Linux após os ajustes acima passou nos 324 testes CTest, com
OpenGL por software e o demo real. Os helpers PowerShell também alinham agora
o CRT do MSVC ao triplet: estático para executável único e packager estático,
dinâmico para exportação em pasta. Os sete testes de orquestração passaram,
incluindo verificação dos dois formatos por comandos simulados. A execução
do jogo Windows de executável único não foi validada por esses testes simulados.

Na execução efetiva dos testes MSVC, o runner não disponibilizou OpenGL 3.3.
Os testes de janela/upload passam a consultar essa capacidade explicitamente;
continuam executados com OpenGL por software no Linux. As fixtures de arquivos
usam diretórios temporários canônicos e expectativas normalizadas para evitar
comparações textuais entre aliases 8.3, caminhos longos e separadores distintos.
O remapeamento de assets e a raiz do browser também normalizam identidades de
caminhos; um teste com alias de diretório cobre a atualização do script movido.
Streams de catálogo/log são fechados antes de substituir ou remover arquivos.
O nome dos chunks Lua usa o prefixo de arquivo `@`, preservando o nome do script
nos erros de caminhos longos. O MSVC compila os fontes UTF-8 explicitamente.

Na BGUI, o teste de menu usa a fonte sintética que ele próprio define; o teste
CMYK considera a conversão com perfil padrão do WIC, mantendo as verificações
estritas de RGB, grayscale e alpha. O teste de macros de projeção foi registrado
explicitamente no CTest. Após essas mudanças, a compilação local e os 326 testes
CTest passaram com OpenGL por software e execução real do demo. As diferenças
exclusivas do WIC e do Windows continuam sendo verificadas pelo Actions.

O MSVC reduziu a suíte a três falhas: separador de caminho em uma expectativa,
colisão entre o executável fictício `Game` e o diretório `game` no Windows, e
medição de conteúdo ignorando os limites mínimos dos filhos na BGUI. A nova
regressão `LinearTest.WrapContentMeasuresChildMinimumDimensions` reproduziu
largura/altura zero antes da correção; após limitar a medida às restrições já
aplicadas no arrange, `LinearTests` e `MenuBarTests` passaram via CTest. A fixture
de publicação usa nome de binário consistente com o manifest e o sistema.

A janela consulta o leitor de recursos existente para o tema, permitindo temas
embutidos em jogos de executável único. O Actions passa a iniciar as duas
exportações Linux em um diretório externo e verificar a inicialização Lua do FPP.

## Cubemap 4×3 e configuração do FPP

O cubemap agora aceita uma imagem em cruz horizontal 4×3. A API anterior e os
campos de seis faces continuam legíveis, enquanto o inspector edita uma única
imagem. O carregamento valida dimensões e restaura o estado de upload OpenGL.
Persistência, snapshots de Play, movimentação de assets e exportação reconhecem
o novo caminho; caminhos com NUL são rejeitados em vez de truncados.

As falhas antigas de cubemap/atmosfera usavam `viewMatrix` diretamente, mas a
câmera calcula a orientação pelo Transform. As regressões agora exercitam esse
contrato e verificam seis faces, cantos, fallback e restauração do estado OpenGL.

No FPP, caminhos absolutos de modelos, escala/orientação da câmera e o script de
pickup colocado em um pilar eram problemas da cena. A cena agora inclui seus
modelos locais e um pickup separado. A movimentação Lua aplicava a rotação do
mouse depois da velocidade e acelerava em diagonais; a ordem e a normalização
foram corrigidas. Testes carregam a cena real, comparam movimento e câmera e
exercitam a coleta sem remover a geometria da arena.

A rodada seguinte aprovou os 320 testes nativos do Windows, mas a exportação
falhou no primeiro teste do compilador por exceder o limite de caminho do MSVC.
O identificador de workspace usa agora os primeiros 16 dígitos SHA-256, como os
identificadores compactos já usados no cache de templates. A regressão de
exportação passou a usar o workspace padrão, incluindo caminhos com espaços;
os sete testes de `platform_build_tests.py` passaram localmente.

Validação local da implementação: 331/331 testes CTest aprovados com OpenGL por
software e smoke de runtime real, antes das verificações adicionais de caminho
malformado, undo/redo do inspector e contato físico da cena. A regressão isolada
do inspector também passou. A matriz remota verificará a implementação em MSVC.

A validação do ZIP oficial de Ubuntu 22.04 em Ubuntu 24.04 reproduziu falha de
janela: o libstdc++ empacotado não exportava GLIBCXX_3.4.32, exigido pelo LLVM/Mesa
do host. Removendo os runtimes C++/GCC do pacote, Play/pausa/continuação/parada e
reinício passaram com o mesmo binário oficial. O bundler agora mantém esses
runtimes no sistema, junto de glibc e dos drivers gráficos; uma regressão compila
um programa C++ e verifica que o pacote não inclui libstdc++ nem libgcc_s.


## Exportação com recursos embutidos e templates — 2026-10-31

Decisões confirmadas pelo usuário: executável único deve ler diretamente do
próprio arquivo, sem autoextração; assets externos devem ser importados para o
projeto. Android permanece futuro. Linux exporta Linux/Windows; Windows exporta
Windows nativamente e Linux via WSL2.

- Jogos exportados em pasta e em executável único não criam o widget de profiling
  nem o overlay F3 de colisores. Runtime de desenvolvimento mantém esses recursos.
- Formato `single`: runtime seguido de recursos comprimidos com zlib, índice e
  footer. Cenas, Lua (`loadfile`/`dofile` incluídos), shaders, temas, fontes,
  modelos/sidecars Assimp e imagens leem do pacote; descompressão em memória.
  Recursos soltos não substituem os embutidos. Escrita de projeto embutido é
  recusada. CRC detecta corrupção, sem promessa de criptografia ou DRM.
- Templates compatíveis são reutilizados por plataforma/formato e fingerprint
  dos fontes do runtime. Ausência de template compila uma vez e guarda em cache;
  template escolhido explicitamente incompatível falha. Inventário valida arquivos
  e tamanhos. Mudança de fontes durante compilação impede publicar cache incorreto.
- Editor compila e distribui o host `game_packager`; CLI aceita ferramenta
  pré-compilada e prepara zlib/vcpkg no Windows quando precisa compilar a própria.
  CI inclui dependência zlib e artefatos de templates. Workflow remoto não executado.
- BGUI usa um provider de recursos compartilhado para imagem, tema e FreeType;
  fontes embutidas dispensam a busca inicial por fontes do sistema. A mudança
  no backend FreeType mantém a memória viva até destruir a face.

A execução real encontrou uma checagem remanescente de arquivo físico em
`commons/src/os/window.cpp`: abortava com tema ausente apesar de ele estar no
pacote. A checagem passou pelo mesmo provider dos demais recursos. Foi adicionado
um teste de leitura de tema embutido e corrigida a simulação de clique do teste de
exportação para usar o evento `on_clicked` do checkbox BGUI.

Validação desta fase ocorreu numa cópia estável dos fontes em
`/tmp/bengine-v3-isolated-src`, baseada no commit `6c338cdf`, com a correção do
tema e a correção já presente no checkout para o caminho mutável do empacotador.
Isso evita misturar alterações simultâneas de release/migração/menus. Apenas na
cópia de teste, caminhos absolutos do exemplo foram apontados para essa cópia e
foi inserido um marcador Lua para comprovar leitura do pacote.

Builds reais Linux x64 e Linux → Windows x64 concluídos. Diretórios de saída
`/tmp/bengine-v3-isolated-export/linux` e `windows` contêm um executável cada.
Linux mantém dependências do sistema gráfico e glibc; verificação de dependências
recusa bibliotecas auxiliares do jogo no formato único. PE importa bibliotecas
do Windows/UCRT, sem DLLs auxiliares do jogo. Windows nativo/MSVC e WSL2 reais não
foram executados neste ambiente; contratos PowerShell/WSL foram simulados.

Smoke Linux: executável copiado para outra pasta, iniciado sem argumentos, cena
carregada e marcador Lua/FPP executados. `strace` não mostrou acesso ao checkout,
projeto fonte ou staging, nem extração de recursos; houve somente escrita no
cache de shaders Mesa do sistema gráfico. Executável com footer truncado encerrou
com código 1 antes da janela. Smoke Windows no Wine: arquivo único iniciou a UI;
segunda execução com o log explícito de scripts confirmou o marcador e os scripts
FPP. Wine não substitui validação no Windows nativo. Os processos foram encerrados
pelo teste; gameplay e encerramento manual não foram avaliados.

Evidências: `/tmp/bengine-v3-smoke-final.log`,
`/tmp/bengine-v3-direct-smoke/files.log`,
`/tmp/bengine-v3-windows-direct-smoke/scripts.bin`,
`/tmp/bengine-v3-final-affected-build.log`,
`/tmp/bengine-v3-relevant-ctest-final.log`,
`/tmp/bengine-v3-script-final.log` e `/tmp/bengine-v3-host-tool-final.log`.
Avisos observados: overrides sem `override` na BGUI e retorno de `write` ignorado
em `runtime_process.cpp`, já presentes; não foram alterados nesta fase.

Validação concluída: editor e runtime compilaram; **39/39 testes CTest relevantes
aprovados**, cobrindo exportação, processo, menu e 11 testes do arquivo embutido.
Os seis testes Python dos fluxos de build também passaram. A configuração MSVC
foi alinhada ao CRT estático dos templates `single` e do host packager estático;
o modo `folder` preserva CRT dinâmico. Essa configuração é coberta pelos contratos
PowerShell simulados, sem alegação de compilação MSVC local.

## Usabilidade do editor: gizmo, importação e edição de objetos — 2026-10-31

Escopo: os cinco pedidos desta conversa. Decisões do usuário: remover da lista e
apagar a pasta são ações separadas; conteúdo idêntico deve ser reutilizado por
padrão, com opção de outra cópia; Ctrl+C/Ctrl+V inclui todos os descendentes.
As hipóteses originais P0/P1/P2 não foram reabertas.

| Item | Status e evidência | Implementação e testes | Limites / revisão |
|---|---|---|---|
| Gizmo de direção sobre janelas e modais | CONFIRMADO. O teste de regressão original recebeu vértices na lista global e nenhuma chamada na fila normal. A prova OpenGL com um painel branco posterior leu RGB `15,18,23` no original e `255,255,255` com a correção. | `direction_gizmo.hpp` gera uma textura local transparente e usa a fila normal de imagens, respeitando a ordem das janelas. A textura só é regenerada quando rotação ou tamanho mudam; identificadores de cache distintos evitam reaproveitar conteúdo de um widget destruído. Cinco testes verificam projeção, escala, input, ordem e atualização da textura. | Sem alteração na arquitetura de renderização ou na BGUI. Aparência com várias janelas arrastadas não foi avaliada manualmente. |
| Imagens e modelos repetidos | CONFIRMADO. A prova original importou `same.png` e depois `same_1.png`; com a correção, ambas as importações retornaram o mesmo caminho. | `asset_import.hpp` compara os bytes; modelos também exigem dependências e configurações equivalentes. Procura dentro do projeto, inclusive em outras pastas. A opção “Importar outra cópia” e a ação “Duplicar arquivo” permitem cópia explícita. Dois testes novos cobrem nomes/pastas diferentes, cópia forçada, conteúdo alterado, dependências e opções. A expectativa antiga de duplicação automática foi atualizada. | Sem migração ou limpeza automática das cópias já existentes. Destinos fora do projeto continuam recusados, mesmo quando existe um recurso igual. |
| Remoção de projetos e arquivos por contexto | PARCIAL no diagnóstico: já havia exclusão de arquivos com confirmação, mas faltavam o menu de contexto e as ações de remover projetos. Os testes de clique direito falharam contra os widgets originais. | Menu nos cartões do Project Manager inicial e nas entradas do modal do editor; ações separadas “Remover da lista” e “Apagar pasta do disco...”. A pasta exige confirmação com caminho completo. O projeto ativo, raiz, cwd/ancestrais e caminhos simbólicos são protegidos. Arquivos oferecem abrir/editar, duplicar, excluir com confirmação e configurar importação 3D. Há testes de lista versus disco, confirmação, contexto em descendentes do cartão e destruição do manager enquanto a confirmação está aberta. | Apagar a pasta é definitivo e inclui todo o seu conteúdo. Projetos legados sem documento `.bengine` válido podem sair da lista, mas não são apagados por essa ação. Menus e confirmações verificam a vida útil do widget de origem antes de executar callbacks. |
| Ctrl+C/Ctrl+V no editor de cena | CONFIRMADO. O teste de atalhos na implementação original não criou a cópia. | `entity_clipboard.hpp` captura uma cópia independente da entidade e dos filhos, remapeia os pais e registra a colagem como uma única ação de desfazer/refazer. Preserva geometria, materiais, configurações de scripts, física, câmeras, nomes e flags de edição. Componentes de câmera recebem o Transform da nova entidade. Testes cobrem cópias repetidas, estado capturado no Ctrl+C, remoção da origem, scripts, instâncias, pai apagado/ID reutilizado e bloqueio durante edição de texto, modal ou Play. | Clipboard interno ao editor e ao projeto atual. Componentes suportados: Transform, Renderer, Camera, Code, Physics, Directional Light, Point Light e Properties; componentes adicionais são recusados com erro, sem colagem parcial. Não copia estado de execução Lua ou velocidades da simulação. A API ganhou `model::clone()` e uma sobrecarga de transferência de instâncias, sem remover assinaturas existentes. |
| Entidades com primitivas | CONFIRMADO: o teste da toolbar falhou contra a Hierarchy original. | “Hierarchy > Primitiva...” cria cubo, esfera, plano ou cilindro com Renderer e Transform, seleciona a entidade e registra desfazer/refazer. Plano e cilindro são assets OBJ da engine; todas as opções têm UVs. Testes sem GPU verificam geometria via Assimp, criação, seleção, histórico e bloqueio em Play. Uma prova OpenGL carregou as quatro opções e verificou VAOs distintos e válidos para suas cópias. | A criação usa a origem da cena. Não houve revisão visual manual dos materiais nem execução nativa no Windows. |

Validação final:

- Build integral em `/tmp/bengine-editor-usability-final`, diretório inicialmente
  novo, incluindo editor, runtime e testes. Foi retomado após alterações
  concomitantes de empacotamento/versionamento; o último build e a atualização
  incremental terminaram sem erros ou avisos nos logs.
- **319/321 entradas CTest aprovadas**, incluindo **46/46 diretamente relacionadas
  aos pedidos**: 33 de workflow de assets, uma do Project Manager, cinco do gizmo
  e sete do importador de modelos. A suíte completa de interação também passou:
  31 testes, incluindo os cinco do gizmo.
- As duas falhas restantes já apareceram na rodada inicial desta validação:
  `EditorRenderTest.CubemapOverridesAtmosphereAndDisableRestoresIt` e
  `LuaApi.FppScriptsMoveLookAndCollectUsingTheNewApi`. No segundo caso, a
  velocidade Z foi 5 em vez de 2,5. Não foram alterados skybox, renderização do
  cubemap ou scripts FPP para contornar essas falhas.
- Provas OpenGL em janela oculta: ordem do gizmo corrigida e quatro primitivas
  com cópias de geometria independentes. Esses testes não constituem avaliação
  manual da interface nem certificação de uma GPU/driver específicos.
- Na árvore concomitante, a declaração `const` do caminho `packager` impedia
  compilar operações posteriores de limpeza/atribuição; apenas esse `const`
  foi removido para desbloquear o build. A nova dependência CURL do verificador
  de releases veio de outro trabalho. Seus headers foram extraídos em `/tmp`
  para a validação, usando a biblioteca compartilhada já instalada; não houve
  instalação global nem novo caminho específico da máquina no repositório.
- Alterações simultâneas de distribuição, releases, persistência, janela, BGUI e
  FPP foram preservadas. Parte da implementação desta tarefa foi incluída em
  commits feitos concomitantemente (`6c338cdf` e `c2faa998`); este relatório
  registra os testes executados nesta conversa, sem atribuir validação às demais
  mudanças desses commits.

Comandos principais, derivados do CMake/workflow:

```sh
cmake -S . -B /tmp/bengine-editor-usability-final -DBUILD_TESTING=ON -DBUILD_EDITOR=ON -DBUILD_RUNTIME=ON -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST="$PWD/out/_deps/googletest-src" -DCURL_INCLUDE_DIR=/tmp/bengine-usability-dependencies/extracted/usr/include/x86_64-linux-gnu -DCURL_LIBRARY=/usr/lib/x86_64-linux-gnu/libcurl.so.4.8.0
cmake --build /tmp/bengine-editor-usability-final --parallel 2
ctest --test-dir /tmp/bengine-editor-usability-final -C Release --output-on-failure
```

Logs: `/tmp/bengine-usability-resume-build.log`,
`/tmp/bengine-usability-final-incremental.log`,
`/tmp/bengine-usability-final-ctest.log`,
`/tmp/bengine-usability-baseline-tests.log`,
`/tmp/bengine-import-baseline.log`, `/tmp/bengine-import-fixed.log`,
`/tmp/bengine-gizmo-baseline-test.log`,
`/tmp/bengine-gizmo-gl-original.log`, `/tmp/bengine-gizmo-final-gl.log` e
`/tmp/bengine-usability-gl-smoke.log`.

### 2026-10-31 — Seleção e rolagem horizontal do inputbox

- Evidência: os testes novos reproduziram Ctrl+A ignorado no campo de uma
  linha (`originalreplacement` em vez de `replacement`) e o texto final fora
  da área visível. Ctrl+End já era processado, mas não revelava o cursor.
- Correção localizada em `external/cpp-bgui`: seleção total também nos campos
  de uma linha e numéricos, seleção com Shift e navegação Home/End que encerra
  a seleção na posição solicitada. A renderização desloca horizontalmente o
  texto para manter o cursor visível; o cálculo do clique usa o mesmo
  deslocamento. O recorte existente e as APIs públicas foram preservados.
- Arquivos: `core/source/elem/inputbox.cpp`, `core/include/elem/text.hpp`,
  `core/source/elem/text.cpp` e `tests/inputbox_tests.cpp` no BGUI.
- Regressões cobertas: Ctrl+A e substituição em texto/números, Ctrl+End,
  Home, setas sucessivas revelando texto oculto, clique após rolagem,
  redimensionamento e navegação após seleção.
- Validação executada: build de `bgui_core` e `all_tests` com
  `cmake --build /tmp/bgui-window-improvements-clean --target all_tests -j2`;
  `ctest --test-dir /tmp/bgui-window-improvements-clean --output-on-failure`
  passou nas 18 suítes, incluindo os 18 casos de `InputAreaTest`.
  Sem warnings nos logs de compilação desta alteração; diff sem erros de
  whitespace.
- Limites: validação headless, com OpenGL/GLFW desativados nesse build.
  Não foi executado o editor completo nem feita inspeção gráfica manual.
  A rolagem adicionada trata os campos de uma linha; o comportamento de
  rolagem de editores multilinha não foi ampliado nesta alteração.

A integração do commit concorrente 7f8846dd preserva a migração de referências
ECS fracas e as alterações de interface. A finalização da janela removia o ECS
e depois o acessava novamente; a limpeza duplicada foi removida. O submódulo
BGUI combina o novo inputbox com as correções de MSVC, medidas e fixtures já
validadas. A cena recém-editada referenciava GLB e script ausentes no repositório;
ela foi preservada integralmente em main.experimental.scene.backup, mantendo main.scene
como exemplo de release autocontido, sem descartar a edição.
