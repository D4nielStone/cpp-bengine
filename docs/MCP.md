# Bubble Engine MCP server

`bengine_mcp` lets a local AI agent inspect a saved Bubble Engine project. It
runs as a separate process over standard input and output (stdio), using the
[2025-11-25 MCP protocol](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

## Build

With the engine's usual build dependencies installed, run from the repository root:

```sh
cmake -S . -B out/mcp -DBUILD_MCP=ON -DBUILD_EDITOR=OFF -DBUILD_RUNTIME=OFF -DBUILD_TESTING=OFF
cmake --build out/mcp --config Release --target bengine_mcp
```

The executable is at `out/mcp/apps/bengine_mcp` with a single-configuration
generator, or `out/mcp/apps/Release/bengine_mcp.exe` with Visual Studio.
`BUILD_MCP` is enabled by default. Pass `-DBUILD_MCP=OFF` to omit the server.

## Connect an agent

In a client that uses an `mcpServers` JSON configuration, add an entry like this.
Replace both paths with absolute paths on your computer.

```json
{
  "mcpServers": {
    "bubble-engine": {
      "command": "/path/to/bengine_mcp",
      "args": ["--project", "/path/to/my-game/project.bengine"]
    }
  }
}
```

On Windows, use the path to `bengine_mcp.exe` and escape backslashes in JSON.
The client launches the server. To try the connection, ask the agent to list
the main scene's entities and inspect one by ID.

## Available tools

| Tool | Arguments | Result |
| --- | --- | --- |
| `project_info` | None | Project format, version, saved settings, main scene reference and entity count. |
| `list_entities` | Optional `offset`, starting at 0 | Up to 100 entities in file order, with IDs, names when saved, parent IDs when saved, and component types. Use `nextOffset` for the next page. |
| `get_entity` | Positive integer `id` | The saved entity record, including serialized component values. |

All tools are read-only. They read the configured `.bengine` file and its main
scene from disk on each call. Legacy projects with inline entities are supported.
The scene must resolve inside the project directory. The editor and runtime can
stay closed. Save in the editor before asking the agent to inspect changes.
Each project or scene file can be up to 32 MiB.
