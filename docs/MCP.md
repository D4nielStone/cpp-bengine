# Bubble Engine creative MCP

`bengine_mcp` is a local stdio MCP server. It can inspect and author saved projects,
create scenes, native meshes, reusable animations and Lua scripts, and launch the
standalone runtime to collect logs and progress screenshots. It uses the existing
project templates, scene/component validators, asset loaders and recoverable file
transactions. The existing inspection tools and `--project` launch remain supported.

## Build and connect

```sh
scripts/build.sh build --test
```

Configure your MCP client with absolute paths (workspace must already exist):

```json
{
  "mcpServers": {
    "bubble-engine": {
      "command": "/path/to/build/apps/bengine_mcp",
      "args": [
        "--workspace", "/path/to/games",
        "--runtime", "/path/to/build/apps/runtime_app"
      ]
    }
  }
}
```

Add `--project /path/to/games/example/project.bengine` to select an existing
project at startup. Without `--workspace`, editing is confined to that project
and creating/switching projects is unavailable. Without `--runtime`, authoring
works but game tests are unavailable. The agent cannot choose arbitrary executables
through tool arguments. On Windows use `.exe` paths and JSON-escaped backslashes.
The server now links the engine's existing authoring and validation code; it needs
its normal installed dependencies, but authoring does not create an OpenGL context.

## Tools

| Tool | Purpose |
| --- | --- |
| `authoring_guide` | Discover the workflow and usable entity, camera, model, animation and script examples. |
| `create_project` | Create/select an `empty` or `fpp` project under workspace; existing folders are rejected. |
| `open_project` | Select a saved `.bengine` under workspace. |
| `project_info` | Inspect saved manifest/main scene metadata. |
| `list_entities` / `get_entity` | Inspect saved main-scene entities (100 per page). |
| `list_files` / `read_file` | Discover project files and read text for editing. |
| `create_model` | Create a unit `cube` or `plane` in `.bmodel` format. |
| `write_asset` | Author/replace `.bmodel`, `.banim`, `.lua`, `.scene` or `.world`. |
| `compose_scene` | Upsert complete entity records and optionally remove IDs in a single validated save. |
| `set_main_scene` | Change the manifest's startup scene. |
| `run_test` | Run the configured runtime, capture a specified frame, then exit. |
| `test_status` | Return running/timeout status, exit code, logs, capture report and MCP PNG image. |
| `stop_test` | Terminate the active runtime without deleting project files. |

Tool discovery supplies complete input schemas. All new tools reject unknown
arguments. Project paths are workspace-relative; asset/scene paths are relative
to the active project directory. Symlinks and `..` segments are rejected.

## A creative workflow

1. `create_project` with `{"path":"floating-island","template":"empty"}`.
2. `authoring_guide` to obtain native examples and serialized component names.
3. `create_model` with `{"path":"assets/models/cube.bmodel","shape":"cube"}`.
   Use `write_asset` for custom meshes, including native rig/skin data. Scale and
   rotate reusable model instances with entity transforms.
4. `write_asset` to create `assets/animations/spin.banim` using the guide's clip,
   and `scripts/example.lua` using its script. Lua is syntax-checked without
   execution. Attach with `animation.clip` and `code.directory` components.
5. `compose_scene` with `path:"island.scene"` and the guide's entity/camera records.
   Add lights, physics and more instances using native component records. A new
   scene starts empty; existing scene metadata and entities not mentioned are kept.
6. `read_file` on `project.bengine`, then `set_main_scene` with `path:"island.scene"`
   and `expectedContents` equal to the exact returned manifest text.
7. `run_test`, then poll `test_status`. Review the PNG against the specification,
   inspect logs, edit and repeat.

Every replacement requires `expectedContents`, the exact text last returned by
`read_file`; omit it when creating a new file. Stale/missing preimages fail without
writing. Entity edits replace the entire selected record, so retain components and
unknown fields you want to keep. Final scene validation rejects invalid components,
duplicate IDs, missing parents and hierarchy cycles. Deletes that orphan children
must update/remove those children in the same call. Clips remain reusable `.banim`
assets; no animation curves are injected into scene component records.

Editing operates on saved files, not on an open editor's unsaved state or undo
history. Close the editor or reload its project after MCP edits. Optimistic content
checks detect prior edits; they are not a filesystem lock against simultaneous
writers. A pending save transaction is refused until recovered in the editor.
Text editing is limited to 400000 bytes per file, MCP requests to 1 MiB, JSON depth
to 64, and inspections to 32 MiB. Larger binary/imported models can already be
placed in the project and referenced by scenes; uploading them is not a tool here.

## Testing and progress screenshots

Example `run_test` arguments:

```json
{
  "width": 1280,
  "height": 720,
  "frames": 120,
  "timeoutSeconds": 30,
  "specification": "A blue rotating cube is centered above the ground, fully visible, with no Lua errors."
}
```

Resolution is 64–2048 pixels per dimension, frame count 1–3600, timeout 1–120
seconds. The runtime enforces the timeout independently of polling (exit code
124), including a hung Lua update. Only one test runs per server; asset writes and project switching are blocked while it runs. The server owns
the child process and stops it at shutdown; no shell is used.

Artifacts persist under `captures/test-<unique-id>.json*` inside the project:

- `.json`: requested dimensions, frame, timeout, project and visual specification.
- `.json.preview.png`: rendered camera framebuffer in top-to-bottom PNG order.
- `.json.report.json`: requested specification, actual captured frame and
  `visualVerdict:"requires_agent_review"`.
- `.json.log`: runtime stdout/stderr; `test_status` returns the last 64 KiB.
- `.json.lua-log`: structured script log used by the existing runtime logger.

`test_status` returns the PNG as an MCP `image` content block, so an image-capable
agent can examine it directly. The screenshot includes the game camera rendering,
not HUD/profiler/editor chrome. It requires a working graphical session and OpenGL.
Simulation uses normal real time, not fixed deterministic steps. Input injection
and interactive gameplay control are not provided; scripted behavior and automated
scene checks can be authored in Lua. Tests execute project scripts with the runtime's
normal permissions; this is not a script sandbox.

A completed process or screenshot does **not** certify gameplay or visual success.
Inspect exit code/logs and compare the actual image with the specification. Missing
captures, startup errors and timeouts must be reported as failures, not as success.

## Validation

`tests/mcp_server_tests.py` exercises discovery, existing inspections, workspace
creation, native authoring, Lua syntax errors, schema validation, stale writes,
path confinement, nesting limits, invalid hierarchies and fixture-based PNG transport over actual stdio.
`RuntimeProcessTest.PreservedProjectSurvivesNaturalExitAndMissingExecutable`
protects the project-preserving launch path. Graphical capture validation requires
a usable OpenGL session; the runtime smoke test is separately opt-in.
