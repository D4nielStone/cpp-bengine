"""Exercise the MCP server over the same stdio transport used by clients."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SERVER = Path(sys.argv.pop(1)).resolve()


def request(method, request_id, **params):
    return {"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}


def call(tool, request_id, **arguments):
    return request("tools/call", request_id, name=tool, arguments=arguments)


INITIALIZE = request(
    "initialize", "hello", protocolVersion="2025-11-25", capabilities={},
    clientInfo={"name": "bengine-mcp-test", "version": "1"},
)
INITIALIZED = {"jsonrpc": "2.0", "method": "notifications/initialized"}


class McpServerTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name) / "project"
        self.root.mkdir()
        self.project = self.root / "project.bengine"
        self.scene = self.root / "cena principal.scene"
        self.manifest = {
            "format": "bubble-engine-project", "version": 2, "project_version": "0.1.0",
            "scene": self.scene.name,
        }
        self.entities = [
            {"id": index, "name": f"Entidade {index} – ação",
             "components": {"transform": {"position": [index, 0, 0]}}}
            for index in range(1, 102)
        ]
        self.write(self.project, self.manifest)
        self.write(self.scene, self.scene_data())

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")

    def scene_data(self):
        return {"format": "bubble-engine-scene", "version": 1, "entities": self.entities}

    def exchange(self, *messages):
        wire = "".join((json.dumps(msg) if isinstance(msg, dict) else msg) + "\n"
                       for msg in messages)
        process = subprocess.run(
            [SERVER, "--project", self.project], input=wire, encoding="utf-8",
            capture_output=True, timeout=5, check=True,
        )
        self.assertEqual(process.stderr, "")
        responses = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertTrue(all(response["jsonrpc"] == "2.0" for response in responses))
        return responses

    def test_handshake_and_discovery(self):
        before, hello, waiting, catalog, ping = self.exchange(
            request("tools/list", 1), INITIALIZE, request("tools/list", 2),
            INITIALIZED, request("tools/list", 3), request("ping", 4),
        )
        self.assertEqual(before["error"]["code"], -32002)
        self.assertEqual(waiting["error"]["code"], -32002)
        self.assertEqual(hello["id"], "hello")
        self.assertEqual(hello["result"]["protocolVersion"], "2025-11-25")
        self.assertIn("tools", hello["result"]["capabilities"])
        tools = catalog["result"]["tools"]
        self.assertEqual({tool["name"] for tool in tools},
                         {"project_info", "list_entities", "get_entity"})
        self.assertTrue(all(tool["annotations"]["readOnlyHint"] for tool in tools))
        self.assertEqual(ping["result"], {})

    def test_saved_scene_queries_preserve_files(self):
        before = {file: file.read_bytes() for file in (self.project, self.scene)}
        _, info, first, second, entity = self.exchange(
            INITIALIZE, INITIALIZED, call("project_info", 1), call("list_entities", 2),
            call("list_entities", 3, offset=100), call("get_entity", 4, id=101),
        )
        metadata = info["result"]["structuredContent"]
        self.assertEqual(metadata["entityCount"], 101)
        self.assertEqual(metadata["project_version"], "0.1.0")
        page = first["result"]["structuredContent"]
        self.assertEqual(len(page["entities"]), 100)
        self.assertEqual(page["nextOffset"], 100)
        last_page = second["result"]["structuredContent"]
        self.assertNotIn("nextOffset", last_page)
        self.assertEqual(last_page["entities"][0]["componentTypes"], ["transform"])
        self.assertEqual(entity["result"]["structuredContent"], self.entities[-1])
        self.assertEqual(json.loads(entity["result"]["content"][0]["text"]), self.entities[-1])
        self.assertEqual({file: file.read_bytes() for file in before}, before)

    def test_legacy_inline_scene(self):
        self.manifest.pop("scene")
        self.manifest.update(version=1, entities=self.entities[:1])
        self.write(self.project, self.manifest)
        _, info = self.exchange(INITIALIZE, INITIALIZED, call("project_info", 1))
        self.assertEqual(info["result"]["structuredContent"]["entityCount"], 1)
        self.assertEqual(json.loads(self.project.read_text(encoding="utf-8"))["version"], 1)

    def test_invalid_requests_and_missing_entity(self):
        parse_error, hello, invalid, missing, ping = self.exchange(
            "{broken json", INITIALIZE, INITIALIZED, call("get_entity", 1, id="1"),
            call("get_entity", 2, id=102), request("ping", 3),
        )
        self.assertEqual(parse_error["error"]["code"], -32700)
        self.assertIsNone(parse_error["id"])
        self.assertEqual(invalid["error"]["code"], -32602)
        self.assertTrue(missing["result"]["isError"])
        self.assertEqual(ping["result"], {})

    def test_scene_cannot_escape_project_directory(self):
        outside = self.root.parent / "outside.scene"
        self.write(outside, self.scene_data())
        self.manifest["scene"] = "../outside.scene"
        self.write(self.project, self.manifest)
        _, result = self.exchange(INITIALIZE, INITIALIZED, call("list_entities", 1))
        self.assertTrue(result["result"]["isError"])
        self.assertIn("outside", result["result"]["content"][0]["text"])

    def test_malformed_and_missing_scene(self):
        self.scene.write_text('{"format": "bubble-engine-scene", "entities": [', encoding="utf-8")
        _, result = self.exchange(INITIALIZE, INITIALIZED, call("list_entities", 1))
        self.assertTrue(result["result"]["isError"])
        self.scene.unlink()
        _, result = self.exchange(INITIALIZE, INITIALIZED, call("list_entities", 1))
        self.assertTrue(result["result"]["isError"])


if __name__ == "__main__":
    unittest.main()
