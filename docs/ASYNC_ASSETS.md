# Asynchronous scene assets

Project loading in the native editor and standalone runtime uses the existing
transactional project loader with optional `asset_loading_hooks`. Existing callers
of `load(path, error)` retain their synchronous behavior and file formats.
The module namespace remains `commons`, as required by the engine configuration.
No change to cpp-bgui is necessary: its modal and progress bar are reused.

## Pipeline and ownership

- `model::prepare(path)` imports CPU geometry, animations and model materials
  without creating GL objects. It throws for invalid or empty imports. Existing
  model constructors retain their behavior. CPU preparation also defers shader
  construction: it must not call OpenGL or access the shared shader cache.
  Clones initialize the default shader on the caller's context thread; models
  used directly initialize it on their first `getShader()`/draw call.
- `scene_asset_loader::prepare(paths, pump)` deduplicates normalized scene paths
  and imports them on a worker. The cache belongs to this scene load; each ECS
  renderer receives an independent clone, including animation state.
- Referenced `.bmat` files, including embedded RGBA decoding, load on the worker
  and are cached for this load.
- The existing scene loader applies serialized material overrides to a staging
  ECS on the context owner thread. Entity/component references never cross into
  the import worker.
- `finish(staging, pump)` resolves and deduplicates the actual external texture
  paths after overrides. Image reading and decoding run on a worker, without
  accessing the shared BGUI asset cache from that worker.
- Texture and mesh uploads run on the calling GL context owner. Bindings are
  restored after each mesh. The pump runs between jobs; gameplay systems are
  paused. BGUI owns uploaded texture cache entries through backend shutdown.
  External texture paths remain external when saved.
- The existing ECS commit occurs after preparation/upload succeeds. Import,
  decoding, upload errors and cancellation preserve the active ECS.

`progress::state` distinguishes importing, decoding, uploading, ready, failed and
cancelled. `fraction` counts completed work in weighted phases: model imports
0–45%, texture decodes 45–65%, mesh/material uploads 65–100%. Empty phases are
skipped. This is task progress, not elapsed time or a byte throughput estimate.
100% means required uploads have returned successfully, before scene activation;
it does not insert a blocking GPU fence.

## UI integration

`load_project_with_modal(project, path, error)` runs on the context owner between
UI frames, with the UI system initialized. Its pump processes window events and
renders only BGUI. It never invokes gameplay systems. The native editor queues
this operation through `window::defer` so button callbacks finish before another
UI frame begins. The runtime invokes it before installing gameplay systems.

The modal shows the scene/project filename, percentage and current asset, and
provides cancellation. The first progress frame is delayed by 150 ms so short
operations can finish without rendering the modal. After that, rendering is
limited to one frame every 16 ms, independently of asset count and worker
polling. Cancellation is checked on every pump call; UI events are processed
on rendered frames. Errors are returned to the existing editor status/error
flow or runtime diagnostic output.

Meshes referencing the same embedded image within an Assimp import share the
decoded pixels. Imported and material-file embedded images compute a non-zero content revision
once on the CPU worker. BGUI uses this revision to avoid hashing every pixel on
every bind. Reloaded content gets a new revision even at the same path. Code that
edits embedded pixels must call `texture::update_embedded_revision()` afterward.

Shared decoded textures reuse their uploaded ID for the duration of `finish`,
avoiding repeated full image hashing by the backend for each material. Model
clones copy geometry directly from const references, retaining independent CPU
storage without temporary vertex/index vectors.

Play snapshot saving also runs material encoding and writing on a worker through
`save_project_with_modal`. The editor dispatches Play between UI frames so the
modal can process events safely. Save cancellation drains the current material
job before returning, and scene/project publication retains its transaction.

## Limits

Cancellation is cooperative between operations. Assimp and image decoding cannot
be interrupted halfway through a call: the worker is drained while UI events
continue, then its result is discarded. No detached worker survives the loader.

Uploads yield between meshes. A single large GL upload, embedded image import,
scene JSON/inline material override parsing, physics construction or script setup can
still exceed a frame budget. This implementation is not chunked GPU streaming
and does not promise a fixed maximum frame time. It keeps the existing per-model
geometry ownership and uses one background job at a time to bound concurrency.
Texture cache entries are retained until BGUI backend shutdown, including entries
uploaded before cancellation. Full CPU scene/import caches are released at the
end of the loading operation.

## Validation

Build `editor_app`, `runtime_app`, `scene_asset_loader_tests`,
`editor_menu_tests`, `editor_persistence_transaction_tests` and
`model_import_tests`. Run CTest matching `SceneAssets|PersistenceTransactionTest|
EditorMenu|ModelImport`. The scene tests cover CPU-only import, deduplication,
clone independence, errors, cancellation/retry, ECS rollback, real GL uploads and
binding preservation, external texture paths and complete modal loading. A cold
shader-cache regression test checks that CPU import leaves the cache untouched
and verifies rendered pixels after the loader is destroyed.
GPU cases skip explicitly when an OpenGL 3.3 display is unavailable.
