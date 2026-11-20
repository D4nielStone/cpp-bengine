"""Exercise live MCP scene editing against the graphical editor."""
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time


def main():
    if os.environ.get("BENGINE_MCP_GRAPHICS_TEST") != "1":
        print("Set BENGINE_MCP_GRAPHICS_TEST=1 with a usable OpenGL display.")
        return 77
    server, editor = (Path(arg).resolve() for arg in sys.argv[1:3])
    engine_version = sys.argv[3]
    with tempfile.TemporaryDirectory(prefix="bubble mcp live ") as directory:
        root = Path(directory)
        project = root / "project.bengine"
        scene = root / "open.scene"
        project.write_text(json.dumps({
            "format": "bubble-engine-project", "version": 2,
            "engine_version": engine_version, "minimum_engine_version": engine_version,
            "project_version": "0.1.0", "scene": scene.name,
        }), encoding="utf-8")
        original = '{"format":"bubble-engine-scene","version":1,"entities":[]}'
        scene.write_text(original, encoding="utf-8")

        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        env = dict(os.environ, BENGINE_EDITOR_MCP_PORT=str(port),
                   BENGINE_EDITOR_MCP_TOKEN=secrets.token_urlsafe(32),
                   BENGINE_MANAGED_UPDATE="1", BENGINE_INSTALLATION=directory)
        with (root / "editor.log").open("w+", encoding="utf-8") as log:
            ui = subprocess.Popen([editor, "--project", project], cwd=root, env=env,
                                  stdout=log, stderr=log)
            mcp = None
            try:
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline and ui.poll() is None:
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.2) as connection:
                            connection.settimeout(2)
                            connection.sendall(b'{"action":"snapshot","token":"wrong"}\n')
                            response = json.loads(connection.makefile("rb").readline())
                            assert "credential" in response["error"], response
                        break
                    except (ConnectionRefusedError, TimeoutError):
                        time.sleep(0.08)
                else:
                    log.seek(0)
                    raise AssertionError(log.read()[-8000:])

                mcp = subprocess.Popen([server, "--editor-port", str(port), "--workspace", directory],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                       stderr=log, encoding="utf-8", env=env)
                sequence = 0

                def send(method, **params):
                    nonlocal sequence
                    sequence += 1
                    mcp.stdin.write(json.dumps({"jsonrpc": "2.0", "id": sequence,
                                                "method": method, "params": params}) + "\n")
                    mcp.stdin.flush()
                    response = json.loads(mcp.stdout.readline())
                    assert "error" not in response, response
                    return response["result"]

                def call(name, expect_failure=False, **arguments):
                    result = send("tools/call", name=name, arguments=arguments)
                    assert bool(result.get("isError")) == expect_failure, result
                    return result if expect_failure else result["structuredContent"]

                send("initialize", protocolVersion="2025-11-25", capabilities={},
                     clientInfo={"name": "live-editor-test", "version": "1"})
                mcp.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
                mcp.stdin.flush()
                assert call("project_info")["scene"] == scene.name
                assert call("list_entities")["total"] == 0
                before = call("read_file", path=scene.name)["contents"]
                call("create_model", path="assets/triangle.bmodel", shape="plane")
                entity = {
                    "id": 7, "name": "Instâncias em edição",
                    "components": {
                        "transform": {"position": [0, 0, -3], "rotation": [0]*3, "scale": [1]*3},
                        "renderer": {
                            "model": "assets/triangle.bmodel", "local_instances": True,
                            "mesh_instances": [[{"position": [1, 0, 0], "rotation": [0]*3, "scale": [1]*3}]],
                        },
                    },
                }
                assert call("compose_scene", path=scene.name, entities=[entity],
                            expectedContents=before)["live"]
                renderer = call("get_entity", id=7)["components"]["renderer"]
                assert renderer["mesh_instances"][0][0]["position"] == [1, 0, 0]
                assert scene.read_text() == original
                call("compose_scene", expect_failure=True, path=scene.name, entities=[],
                     removeIds=[7], expectedContents=before)
                assert call("undo_live_edit")["applied"]
                assert call("list_entities")["total"] == 0
                assert call("redo_live_edit")["applied"]
                call("save_live_project")
                assert json.loads(scene.read_text(encoding="utf-8"))["entities"][0]["id"] == 7
                print("Verified live port authentication, asset creation, stale edits, undo, redo and save.")
            finally:
                if mcp is not None:
                    mcp.terminate()
                    mcp.wait(timeout=5)
                ui.terminate()
                ui.wait(timeout=5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
