# Estabilização e validação

Este documento é um guia operacional, não um histórico automático de commits
nem um relatório de testes executados em cada máquina.

## Build e testes

Linux:

```sh
./scripts/build.sh
./scripts/build.sh /tmp/cpp-bengine-build --test
```

Windows:

```powershell
.\scripts\build.ps1
.\scripts\build.ps1 -RunTests
```

On Windows, the test helper excludes the upstream BGUI `MenuBarTests` and
`ImageTests`: their current geometry and JPEG fixture assertions fail on the
Windows runner. These exclusions are explicit and do not indicate that those
third-party tests passed.

Para validar um alvo focado em um build CMake já configurado:

```sh
cmake --build out/build/Debug --target editor_app
ctest --test-dir out/build/Debug --output-on-failure
```

Use `ctest -R <expressão>` para selecionar casos específicos. Informe o comando
exato e o resultado observado; não descreva compilação como teste, nem declare
sucesso para uma execução que não terminou.

## Checklist de mudanças

- Reproduza a falha e identifique os módulos e formatos envolvidos.
- Faça a menor correção completa, sem remover trabalho local não relacionado.
- Adicione regressão para comportamento e para persistência quando aplicável.
- Valide build dos alvos afetados e testes relevantes.
- Revise warnings, recursos GPU e estado de projetos após falhas.
- Atualize a documentação e declare o que ficou sem validação.

## Áreas que pedem validação manual

Renderização e interação dependem de contexto OpenGL, drivers, display, DPI e
entrada nativa. Exportação depende do alvo, bibliotecas e recursos empacotados.
Os testes que pulam por falta de display não contam como confirmação gráfica.

Para uma avaliação de critérios mais ampla, veja
[STABILITY_ASSESSMENT.md](./STABILITY_ASSESSMENT.md).

### Editor workflow and viewport follow-up (2026-11-02)

- The model toolbar's distance input was inheriting a 100-pixel layout minimum;
  explicit bounds and pixel sizing now keep it inside the toolbar. Mode and tool
  buttons have explicit dimensions and spacing. A layout regression checks the
  distance field and adjacent action.
- Camera atmosphere, horizon, fog and directional-light RGB controls reuse the
  existing color picker with undo/redo. The editor camera is also inspectable
  when no scene entity is selected.
- `.editor-workflow.json` adds atomic session storage for workspace/model modes,
  orbital view, selection, inspector section expansion, collider visibility,
  extrusion distance and editor camera configuration/transform. Existing project
  and interface formats remain unchanged. Workspace saving precedes project
  replacement; malformed workflow JSON reports an error.
- `O` toggles normal/orbital navigation over the scene, respecting text focus and
  modal input. Losing the selected orbital target releases its retained pivot.
- Game remains available during playback and respects an explicitly closed panel.
  The runtime publishes transient transform/camera snapshots at up to 30 Hz;
  the editor consumes changed snapshots and restores pre-play values on stop.
  Save, project replacement and exit stop playback before persisting the scene.
  Preview files are removed with the runtime snapshot. Runtime-only entity
  creation/removal, material changes and HUD are not mirrored by this protocol.
- Files actions open from a left click on the file area's background. Existing
  per-file operations reuse the same lifetime-safe popup. Braces also correct a
  dangling `else` introduced by model filtering that omitted folders from the grid.
- BGUI button labels render `...` inside the available width without changing
  their original text; truncation preserves UTF-8 boundaries and expansion
  restores the full label. This required focused changes in the BGUI submodule.
- Validation: 398 CTest cases passed with `BENGINE_RUNTIME_SMOKE=1`, including
  OpenGL rendering, FPP start/pause/resume/stop/restart, preview publication and
  cleanup, workflow round trips, file context actions, model toolbar sizing,
  color undo/redo and label truncation. Interactive appearance and keyboard feel
  still require human review; Windows execution was not exercised on this host.

### Model panel selection and corner square (2026-11-02)

- Root cause: `model_editor_view::update()` had no per-frame caller in the running
  editor. The model surface now drives its panel update through BGUI's existing
  UI lifecycle. Selecting another scene entity loads its model without requiring
  a manual panel update or a separate file selection.
- The panel now has object/source identification, selection modes, a large preview
  and a tools sidebar. Geometry comes from the selected renderer's CPU meshes;
  source polygons are retained only when their vertices and indices match the
  live model. Models without an asset file are supported and can be saved into
  the project's `models` folder without overwriting an existing generated file.
  Saving preserves the selected model's material and instance settings.
- The corner square has a separate cause from the old distance-field layout:
  `ui_scale_system` and `undo_redo_shortcuts_system` inherited the default element
  renderer. Both keyboard-only controllers now emit no draw requirements, even
  if their visual style is marked visible. Keyboard behavior is unchanged.
- Regression coverage checks UI-frame selection changes, in-memory geometry,
  save/reload, component removal, sidebar bounds and both controllers' empty
  draw queues. Manual visual review and Windows execution remain unverified.

### Model transform gizmo (2026-11-03)

- The model preview reuses `editor::transform_gizmo` with an orthographic matrix
  matching its CPU projection. G/R/S manipulate the whole model or selected
  faces/vertices/edges around their centroid. Preview framing remains stable
  during editing, with an explicit frame-model action.
- Picking defers to the gizmo's axis hit test so a transform drag does not also
  change the geometry selection. Input respects modal windows and text fields;
  deleting or switching the source resets the retained gizmo target.
- Saving updates the selected renderer's CPU geometry in place and invalidates
  loaded mesh buffers for their next draw. This preserves materials, instances
  and shader state and avoids loading GPU resources merely to save model data.
- Validation completed: editor/runtime and test targets built successfully;
  `BENGINE_RUNTIME_SMOKE=1 ctest --test-dir out/build/Debug --output-on-failure -j 2`
  passed all 401 tests. This includes an actual projected-X gizmo drag followed
  by save/reload, checking that mesh geometry changes while the scene entity's
  transform stays unchanged. Full interactive visual review remains manual.

### Constant-size gizmo, model navigation and mesh materials (2026-11-03)

- Removed world-unit size clamps that changed gizmo size at extreme zoom. Axis
  endpoints now use a fixed screen length; rotation radii converge without
  world-unit clamps. Invisible axes no longer emit a line to the default screen
  origin. Regression coverage measures the gizmo at zoom factors 1e-6, 1 and 1e6.
- Model navigation reuses `camera_movement_system` with a private preview camera
  and right-button navigation. Normal view looks around from the current eye;
  orbital view moves around the model pivot. The O shortcut and button switch
  modes; workflow persistence stores the model camera mode independently of Scene.
- `Malhas` has one collapsible section per mesh. A shared material editor now
  supplies both Inspector and model controls, with PBR factors, texture maps,
  UV controls, texture previews, drop targets and undo/redo. Bindings validate the
  renderer's identity and lifetime before applying changes or history actions.
- CPU preview projects geometry with the preview camera, clips offscreen wire
  edges and samples the albedo map with perspective-correct UVs. Full PBR lighting
  remains in the engine renderer. Materials are saved with the project, whereas
  `.bmodel` continues to preserve its geometry-only format.
- Validation completed: build succeeded and all 403 CTest tests passed with
  `BENGINE_RUNTIME_SMOKE=1`. New checks cover extreme zoom and near-camera depth
  axes, independent per-mesh material/texture edits with undo, persisted model
  navigation mode, orbital eye movement and normal-view rotation in place.
  Inspector material regressions and the existing rendering/runtime tests also
  passed. Manual visual inspection and Windows execution remain unverified.

### v0.1.3 Windows release validation (2026-11-03)

- Retrieved failing Windows job 125448328382: removed demo fixtures caused missing
  files; Bullet shapes built without RTTI caused dynamic_cast access violations.
  Pending template fixtures and the directly retained convex hull pointer address
  these failures without changing public interfaces.
- BGUI popup measurement ignored wrap-content child minimum constraints (32px
  child in 31px popup). Clamp measured content to the same limits used to arrange
  children; add a platform-independent minimum-size regression.
- WIC maps the unprofiled CMYK red fixture to RGB (228, 50, 43); libjpeg uses
  the direct CMYK conversion. Assert the Windows conversion explicitly while
  preserving strict RGB/grayscale/progressive checks and opaque RGBA checks.
- Remove Windows test exclusions. Local Debug build and all 403 CTest entries
  passed before the final BGUI measurement regression and version bump; final
  local and native Windows results are recorded in the release validation.
