"""Exercise the MCP server over the same stdio transport used by clients."""
import json
import os
import base64
import time
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
        responses = [json.loads(line) for line in process.stdout.splitlines()]
        self.assertTrue(all(response["jsonrpc"] == "2.0" for response in responses))
        return responses

    def test_compose_preserves_untouched_legacy_asset_paths(self):
        for entity in self.entities:
            entity["components"]["transform"].update(rotation=[0, 0, 0], scale=[1, 1, 1])
        self.entities[0]["components"]["renderer"] = {"model": "/legacy/engine/cube.bmodel"}
        self.write(self.scene, self.scene_data())
        before = self.scene.read_text()
        addition = {"id": 102, "components": {"transform": {"position": [0, 0, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1]}}}
        _, result = self.exchange(INITIALIZE, INITIALIZED, call("compose_scene", 1,
            path=self.scene.name, entities=[addition], expectedContents=before))
        self.assertNotIn("isError", result["result"])
        saved = json.loads(self.scene.read_text())
        self.assertEqual(saved["entities"][:101], self.entities)
        changed = dict(addition, components={"renderer": {"model": "/outside/model.bmodel"}})
        contents = self.scene.read_text()
        _, rejected = self.exchange(INITIALIZE, INITIALIZED, call("compose_scene", 2,
            path=self.scene.name, entities=[changed], expectedContents=contents))
        self.assertTrue(rejected["result"]["isError"])
        self.assertEqual(self.scene.read_text(), contents)

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
        self.assertTrue({"project_info", "list_entities", "get_entity", "create_project",
                         "compose_scene", "create_model", "write_asset", "run_test", "test_status"}
                        <= {tool["name"] for tool in tools})
        self.assertTrue(all(tool["annotations"]["readOnlyHint"] for tool in tools
                            if tool["name"] in {"project_info", "list_entities", "get_entity"}))
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


    def test_shader_sources_use_confined_conflict_checked_asset_writes(self):
        source = "#version 330 core\nout vec4 color; void main(){color=vec4(1); }"
        _, created, vertex, stale, changed, empty, nul, outside, readback = self.exchange(
            INITIALIZE, INITIALIZED,
            call("write_asset", 1, path="shaders/post.frag", contents=source),
            call("write_asset", 2, path="shaders/post.vert", contents=source),
            call("write_asset", 3, path="shaders/post.frag", contents=source, expectedContents="stale"),
            call("write_asset", 4, path="shaders/post.frag", contents=source + "\n", expectedContents=source),
            call("write_asset", 5, path="shaders/empty.frag", contents=""),
            call("write_asset", 6, path="shaders/nul.frag", contents="x\0y"),
            call("write_asset", 7, path="../outside.frag", contents=source),
            call("read_file", 8, path="shaders/post.frag"),
        )
        for result in (created, vertex, changed, readback):
            self.assertNotIn("isError", result["result"])
        for result in (stale, empty, nul, outside):
            self.assertTrue(result["result"]["isError"])
        self.assertEqual(readback["result"]["structuredContent"]["contents"], source + "\n")

    def test_asset_writes_validate_syntax_and_detect_conflicts(self):
        _, bad, created, conflict, changed, readback = self.exchange(
            INITIALIZE, INITIALIZED,
            call("write_asset", 1, path="scripts/a.lua", contents="function !!!"),
            call("write_asset", 2, path="scripts/a.lua", contents="function setup() end"),
            call("write_asset", 3, path="scripts/a.lua", contents="", expectedContents="stale"),
            call("write_asset", 4, path="scripts/a.lua", contents="function update() end",
                 expectedContents="function setup() end"),
            call("read_file", 5, path="scripts/a.lua"),
        )
        self.assertTrue(bad["result"]["isError"])
        self.assertNotIn("isError", created["result"])
        self.assertTrue(conflict["result"]["isError"])
        self.assertNotIn("isError", changed["result"])
        self.assertEqual(readback["result"]["structuredContent"]["contents"], "function update() end")

    def test_authoring_examples_and_native_assets(self):
        _, response = self.exchange(INITIALIZE, INITIALIZED, call("authoring_guide", 1))
        guide = response["result"]["structuredContent"]
        self.assertGreater(guide["entityExample"]["id"], 1)
        self.assertGreater(guide["cameraExample"]["id"], 1)
        self.assertNotEqual(guide["entityExample"]["id"], guide["cameraExample"]["id"])
        _, model, clip, script, scene = self.exchange(
            INITIALIZE, INITIALIZED,
            call("create_model", 1, path="assets/cube.bmodel", shape="cube"),
            call("write_asset", 2, path="assets/spin.banim", contents=json.dumps(guide["animationExample"])),
            call("write_asset", 3, path="scripts/example.lua", contents=guide["scriptExample"]),
            call("compose_scene", 4, path="new.scene", entities=[guide["entityExample"], guide["cameraExample"]]),
        )
        for item in (model, clip, script, scene):
            self.assertNotIn("isError", item["result"], item)
        self.assertEqual(scene["result"]["structuredContent"]["entityCount"], 2)
        saved = json.loads((self.root / "assets/cube.bmodel").read_text())
        self.assertEqual(len(saved["meshes"][0]["faces"]), 6)

    def test_paths_schemas_and_invalid_scenes_preserve_files(self):
        outside = self.root.parent / "outside.lua"
        outside.write_text("original")
        (self.root / "linked.lua").symlink_to(outside)
        before = self.scene.read_bytes()
        invalid = {"id":1,"parent":1,"components":{"transform":{
            "position":[0,0,0],"rotation":[0,0,0],"scale":[1,1,1]}}}
        _, escape, symlink, invalid_scene, schema, deep = self.exchange(
            INITIALIZE, INITIALIZED,
            call("write_asset", 1, path="../outside.lua", contents=""),
            call("write_asset", 2, path="linked.lua", contents="", expectedContents="original"),
            call("compose_scene", 3, path="new.scene", entities=[invalid]),
            call("run_test", 4, specification="cube", frames=0),
            '{"jsonrpc":"2.0","id":5,"params":' + '[' * 70 + '0' + ']' * 70 + '}',
        )
        for result in (escape, symlink, invalid_scene):
            self.assertTrue(result["result"]["isError"])
        self.assertEqual(schema["error"]["code"], -32602)
        self.assertEqual(deep["error"]["code"], -32700)
        self.assertEqual(outside.read_text(), "original")
        self.assertEqual(self.scene.read_bytes(), before)
        self.assertFalse((self.root / "new.scene").exists())

    def test_scene_updates_preserve_unmentioned_data_and_validate_deletions(self):
        pose = {"position":[0,0,0], "rotation":[0,0,0], "scale":[1,1,1]}
        entities = [{"id":1,"name":"Root","custom":"keep","components":{"transform":pose}},
                    {"id":2,"parent":1,"components":{"transform":pose}}]
        scene = {"format":"bubble-engine-scene", "version":1, "environment":"keep", "entities":entities}
        original = json.dumps(scene)
        _, created, invalid = self.exchange(INITIALIZE, INITIALIZED,
            call("write_asset", 1, path="editable.scene", contents=original),
            call("compose_scene", 2, path="editable.scene", entities=[], removeIds=[1],
                 expectedContents=original))
        self.assertNotIn("isError", created["result"])
        self.assertTrue(invalid["result"]["isError"])
        self.assertEqual((self.root / "editable.scene").read_text(), original)
        updated = dict(entities[1], name="Child")
        _, changed = self.exchange(INITIALIZE, INITIALIZED,
            call("compose_scene", 1, path="editable.scene", entities=[updated], expectedContents=original))
        self.assertNotIn("isError", changed["result"])
        saved = json.loads((self.root / "editable.scene").read_text())
        self.assertEqual(saved["environment"], "keep")
        self.assertEqual(saved["entities"][0], entities[0])
        self.assertEqual(saved["entities"][1]["name"], "Child")
        manifest = self.project.read_text()
        _, switched = self.exchange(INITIALIZE, INITIALIZED,
            call("set_main_scene", 1, path="editable.scene", expectedContents=manifest))
        self.assertNotIn("isError", switched["result"])
        self.assertEqual(json.loads(self.project.read_text())["scene"], "editable.scene")

    @unittest.skipIf(os.name == "nt", "Executable Python fixture uses a POSIX shebang")
    def test_runtime_protocol_preserves_project_and_returns_image(self):
        fixture = self.root.parent / "runtime fixture"
        image = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aSFAAAAAASUVORK5CYII="
        fixture.write_text("#!" + sys.executable + "\n" +
            "import sys,json,base64\nfrom pathlib import Path\n" +
            "args=dict(zip(sys.argv[1::2],sys.argv[2::2]))\n" +
            "spec=Path(args['--test-spec'])\n" +
            "report=json.loads(spec.read_text())\nreport['capturedFrame']=report['frames']\n" +
            "Path(str(spec)+'.report.json').write_text(json.dumps(report))\n" +
            "Path(str(spec)+'.preview.png').write_bytes(base64.b64decode(" + repr(image) + "))\n" +
            "print('fixture stdout must not corrupt MCP')\n" +
            "print('fixture diagnostic',file=sys.stderr)\n")
        fixture.chmod(0o700)
        before = {p:p.read_bytes() for p in (self.project,self.scene)}
        process = subprocess.Popen([SERVER,"--project",self.project,"--runtime",fixture],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        def send(message):
            process.stdin.write(json.dumps(message)+"\n")
            process.stdin.flush()
            response=json.loads(process.stdout.readline())
            self.assertNotIn("error",response)
            self.assertNotIn("isError",response["result"])
            return response["result"]
        try:
            send(INITIALIZE)
            process.stdin.write(json.dumps(INITIALIZED)+"\n")
            send(call("run_test",1,specification="Protocol fixture only",frames=1))
            deadline=time.monotonic()+5
            while True:
                result=send(call("test_status",2))
                status=result["structuredContent"]
                if not status["running"]: break
                self.assertLess(time.monotonic(),deadline)
                time.sleep(.02)
            self.assertEqual(status["exitCode"],0)
            self.assertIn("fixture diagnostic",status["log"])
            self.assertIn("fixture stdout",status["log"])
            content=[item for item in result["content"] if item["type"]=="image"]
            self.assertEqual(base64.b64decode(content[0]["data"]),base64.b64decode(image))
            self.assertEqual({p:p.read_bytes() for p in before},before)
        finally:
            process.stdin.close()
            try: process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(timeout=5)
            process.stdout.close()
            process.stderr.close()

    def test_workspace_creation_and_selection(self):
        messages = [INITIALIZE, INITIALIZED,
            call("create_project", 1, path="creative", template="empty"),
            call("project_info", 2),
            call("create_project", 3, path="creative", template="fpp"),
            call("create_project", 4, path="../escaped", template="empty"),
            call("create_project", 5, path="first-person", template="fpp"),
            call("open_project", 6, path="creative/project.bengine")]
        completed = subprocess.run([SERVER, "--workspace", self.root],
            input="".join(json.dumps(item) + "\n" for item in messages), text=True,
            capture_output=True, check=True, timeout=15)
        results = [json.loads(line) for line in completed.stdout.splitlines()]
        self.assertNotIn("isError", results[1]["result"])
        self.assertEqual(results[2]["result"]["structuredContent"]["entityCount"], 0)
        self.assertTrue(results[3]["result"]["isError"])
        self.assertTrue(results[4]["result"]["isError"])
        self.assertNotIn("isError", results[5]["result"])
        self.assertNotIn("isError", results[6]["result"])
        self.assertTrue((self.root / "first-person/scripts/fpp_controller.lua").exists())


if __name__ == "__main__":
    unittest.main()
