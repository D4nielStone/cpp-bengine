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

## Live editor connection

Start the editor and configure the MCP client with the same random credential.
The editor binds to IPv4 loopback only when `BENGINE_EDITOR_MCP_PORT` and a
valid `BENGINE_EDITOR_MCP_TOKEN` are set.

```sh
export BENGINE_EDITOR_MCP_PORT=8766
export BENGINE_EDITOR_MCP_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
/path/to/build/apps/editor_app --project "/path/to/game/project.bengine"
```

Configure the MCP client to start the following command and pass it the same
`BENGINE_EDITOR_MCP_TOKEN` environment value.

```sh
/path/to/build/apps/bengine_mcp --editor-port 8766 --workspace /path/to/games
```

The workspace limit is optional. The connection follows the project selected
in that editor. Start another editor with a distinct port for a separate session.
The port carries a local editor protocol. MCP clients still speak stdio to
`bengine_mcp` or HTTP through the existing adapter.

`project_info`, `list_entities`, `get_entity` and `read_file` show unsaved
changes to the active scene. Read its current contents before calling
`compose_scene` or `write_asset` on it. The editor validates each change
against that version on its own thread and records one undo action. Locked
entities and the editor camera are protected. With no play session active,
`undo_live_edit`, `redo_live_edit` and `save_live_project` use the editor's
history and save flow. Scene switching observes the unsaved-change guard.

Asset writes use the editor's file transaction and check the current disk
contents. Existing custom shaders refresh through the shader loader. Assets
already instantiated in the scene, including native meshes and animation
clips, may need to be reassigned or reopened to load a modified asset file.
`run_test` starts a separate runtime from the saved project. Save from the
editor first to include unsaved scene edits. The inspector and the MCP share
the scene state. Save pending text edits before editing the same file through
MCP. Clean open documents are refreshed after a successful MCP file write.

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
| `write_asset` | Author/replace `.bmodel`, `.banim`, `.lua`, `.vert`, `.frag`, `.scene` or `.world`. |
| `compose_scene` | Upsert complete entity records and optionally remove IDs in a single validated save. |
| `set_main_scene` | Change the manifest's startup scene. |
| `run_test` | Run the configured runtime, capture a specified frame, then exit. |
| `test_status` | Return running/timeout status, exit code, logs, capture report and MCP PNG image. |
| `stop_test` | Terminate the active runtime without deleting project files. |

Entity ID 1 is reserved for the internal editor/runtime camera; the authoring
guide uses IDs 2 and 3.

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

Standalone mode edits saved files. Reload the project in an open editor after
standalone MCP edits. Optimistic content
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

## Authenticated local HTTP transport

The optional Python 3 standard-library adapter preserves the C++ stdio server and
its complete tool catalog. Start it with a randomly generated credential of at
least 32 ASCII characters in `BENGINE_MCP_TOKEN` (do not commit credentials):

```sh
export BENGINE_MCP_TOKEN="$(python3 -c 'import secrets; print(secrets.token_urlsafe(32))')"
python3 apps/mcp/http_transport.py --port 8765 -- \
  /path/to/build/apps/bengine_mcp --project /path/to/game/project.bengine \
  --runtime /path/to/build/apps/runtime_app
```

The endpoint is `http://127.0.0.1:8765/mcp`. Every request requires
`Authorization: Bearer <credential>`. POST requires `Content-Type: application/json`
and `Accept: application/json, text/event-stream`. Initialize normally, retain the
returned `Mcp-Session-Id`, and send it plus `MCP-Protocol-Version: 2025-11-25` on
subsequent requests, including `notifications/initialized`. Notifications return
202; requests return JSON. GET returns 405 (no unsolicited SSE stream); DELETE
ends the session. A second writer receives 409. Session inactivity of five minutes
is checked on the next POST/DELETE; DELETE or shutdown stops the child runtime.
Use only one adapter per project. Existing optimistic file checks still apply.
`compose_scene` preserves untouched legacy records with absolute native asset paths;
references in every inserted or replaced entity must remain project-relative.

The adapter binds exclusively to IPv4 loopback, checks Host against localhost and
127.0.0.1 with the actual port, rejects browser Origins unless explicitly allowed
with repeatable `--origin https://trusted.example`, and compares credentials in
constant time. It limits concurrent connections, request bytes, response bytes and
socket/subprocess waits. It does not log credentials or request bodies, and removes
the token from the environment inherited by engine processes. Only trusted callers
should receive the credential: tools can write project assets and execute Lua in
the runtime with the user's permissions.

This is a local bearer authentication mode, **not an OAuth authorization server**.
Remote ChatGPT connection is not configured by this command. It requires a reachable
HTTPS endpoint and an OAuth 2.1 resource/authorization gateway implementing MCP
protected-resource discovery, token audience validation and the client's supported
registration flow. Configure that gateway to forward authenticated traffic only,
rewrite Host to the loopback authority, and inject a separate private adapter
credential; never expose the raw adapter port or reuse a remote access token as its
local credential. No public URL, TLS certificate or OAuth provider is provisioned
by this repository. See the [MCP transport specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)
and [authorization specification](https://modelcontextprotocol.io/specification/2025-11-25/basic/authorization).

HTTP regression coverage lives in `tests/mcp_http_tests.py` and CTest `mcp_http`.
HTTP access continues to edit saved files only; it cannot inspect unsaved editor
state, drive editor commands, save an open scene, or capture the editor viewport.

GLSL `.vert`/`.frag` authoring uses the same confined paths, size limits,
recoverable transactions and exact `expectedContents` checks. Empty and NUL-bearing
sources are rejected. The headless MCP server does not compile or certify GLSL;
compile in the editor/runtime and inspect logs plus the resulting capture. Camera
filters and renderer material shader references use the existing generic uniform
controls; see `docs/lua-api/globals-and-shaders.md`.
