#pragma once
namespace bubble::mcp {
inline constexpr auto tool_catalog = R"json({
  "tools": [
    {"name":"save_live_project","description":"Save the current scene and project from the connected editor.","inputSchema":{"type":"object","additionalProperties":false}},
    {"name":"undo_live_edit","description":"Undo the most recent command in the connected editor.","inputSchema":{"type":"object","additionalProperties":false}},
    {"name":"redo_live_edit","description":"Redo the most recent undone command in the connected editor.","inputSchema":{"type":"object","additionalProperties":false}},
    {
      "name": "project_info",
      "description": "Read project metadata and scene entity count from the open editor or saved project.",
      "inputSchema": {
        "type": "object",
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "list_entities",
      "description": "List scene entities and component types from the open editor or saved project.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "offset": {
            "type": "integer",
            "minimum": 0,
            "maximum": 4294967295
          }
        },
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "get_entity",
      "description": "Read an entity and its serialized components by ID.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "integer",
            "minimum": 1,
            "maximum": 4294967295
          }
        },
        "required": [
          "id"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "create_project",
      "description": "Create and select a project using the editor Empty/FPP templates. Requires --workspace. Never overwrites a folder.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          },
          "template": {
            "type": "string",
            "enum": [
              "empty",
              "fpp"
            ]
          }
        },
        "required": [
          "path",
          "template"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "open_project",
      "description": "Select an existing .bengine within --workspace.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          }
        },
        "required": [
          "path"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "list_files",
      "description": "List project assets, scenes and scripts, 100 per page.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "offset": {
            "type": "integer",
            "minimum": 0,
            "maximum": 10000
          }
        },
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false,
        "openWorldHint": false
      }
    },
    {
      "name": "read_file",
      "description": "Read a UTF-8 text asset or manifest (max 400000 bytes); use contents as expectedContents on edits.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          }
        },
        "required": [
          "path"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false,
        "openWorldHint": false
      }
    },
    {
      "name": "write_asset",
      "description": "Create or replace Lua scripts (syntax checked), GLSL .vert/.frag source (compiled later by editor/runtime), .banim clips, .bmodel meshes, and .scene/.world scenes. Uses native formats and validators. Disk editing only: close the editor or reload after edits. Read authoring_guide for examples.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          },
          "contents": {
            "type": "string"
          },
          "expectedContents": {
            "type": "string",
            "description": "Exact contents from read_file, required when replacing an existing file. Omit for new files."
          }
        },
        "required": [
          "path",
          "contents"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "set_main_scene",
      "description": "Set a valid saved scene as startup scene. expectedContents must match project manifest.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          },
          "expectedContents": {
            "type": "string",
            "description": "Exact contents from read_file, required when replacing an existing file. Omit for new files."
          }
        },
        "required": [
          "path",
          "expectedContents"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "compose_scene",
      "description": "Upsert complete entity records and optionally remove IDs in one validated scene transaction. Existing entities with matching IDs are replaced; other entities and scene metadata are preserved. New scene created if absent.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          },
          "entities": {
            "type": "array",
            "items": {
              "type": "object"
            }
          },
          "removeIds": {
            "type": "array",
            "items": {
              "type": "integer",
              "minimum": 1
            }
          },
          "expectedContents": {
            "type": "string",
            "description": "Exact contents from read_file, required when replacing an existing file. Omit for new files."
          }
        },
        "required": [
          "path",
          "entities"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "create_model",
      "description": "Create a unit cube or plane native .bmodel. Scale/rotate instances using scene transforms.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "path": {
            "type": "string",
            "minLength": 1,
            "description": "Relative path inside the configured workspace (projects) or active project (assets). No symlinks or .. segments."
          },
          "shape": {
            "type": "string",
            "enum": [
              "cube",
              "plane"
            ]
          },
          "expectedContents": {
            "type": "string",
            "description": "Exact contents from read_file, required when replacing an existing file. Omit for new files."
          }
        },
        "required": [
          "path",
          "shape"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "authoring_guide",
      "description": "Get native model, scene, animation and script examples and the recommended creative workflow.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false,
        "openWorldHint": false
      }
    },
    {
      "name": "run_test",
      "description": "Launch the configured runtime on the saved project. Runs Lua scripts. Captures game camera framebuffer (no HUD) after frames and exits. Requires --runtime. Call test_status for logs, report and PNG. Visual specification is for agent review, not an automated pass verdict.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "width": {
            "type": "integer",
            "minimum": 64,
            "maximum": 2048
          },
          "height": {
            "type": "integer",
            "minimum": 64,
            "maximum": 2048
          },
          "frames": {
            "type": "integer",
            "minimum": 1,
            "maximum": 3600
          },
          "timeoutSeconds": {
            "type": "integer",
            "minimum": 1,
            "maximum": 120
          },
          "specification": {
            "type": "string",
            "minLength": 1,
            "maxLength": 8192
          }
        },
        "required": [
          "specification"
        ],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    },
    {
      "name": "test_status",
      "description": "Poll active test, get process exit code, bounded logs, specification and PNG image when complete.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": true,
        "destructiveHint": false,
        "openWorldHint": false
      }
    },
    {
      "name": "stop_test",
      "description": "Stop the active runtime test.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "required": [],
        "additionalProperties": false
      },
      "annotations": {
        "readOnlyHint": false,
        "destructiveHint": true,
        "openWorldHint": false
      }
    }
  ]
})json";
}
