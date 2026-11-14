"""Opt-in real OpenGL test of the complete MCP authoring/capture workflow."""
import base64
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time


def main():
    if os.environ.get("BENGINE_MCP_GRAPHICS_TEST") != "1":
        print("Set BENGINE_MCP_GRAPHICS_TEST=1 with a usable OpenGL display.")
        return 77
    server, runtime = (Path(arg).resolve() for arg in sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix="bubble mcp capture ") as directory:
        process = subprocess.Popen([server, "--workspace", directory, "--runtime", runtime],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        sequence = 0

        def send(method, **params):
            nonlocal sequence
            sequence += 1
            process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": sequence,
                                            "method": method, "params": params}) + "\n")
            process.stdin.flush()
            response = json.loads(process.stdout.readline())
            assert "error" not in response, response
            result = response["result"]
            assert not result.get("isError"), result
            return result

        def call(name, **args):
            return send("tools/call", name=name, arguments=args)

        try:
            send("initialize", protocolVersion="2025-11-25", capabilities={},
                 clientInfo={"name":"capture-test", "version":"1"})
            process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
            process.stdin.flush()
            call("create_project", path="game", template="empty")
            guide = call("authoring_guide")["structuredContent"]
            call("create_model", path="assets/models/cube.bmodel", shape="cube")
            call("write_asset", path="assets/animations/spin.banim",
                 contents=json.dumps(guide["animationExample"]))
            call("write_asset", path="scripts/example.lua", contents=guide["scriptExample"])
            call("compose_scene", path="capture.scene",
                 entities=[guide["entityExample"], guide["cameraExample"]])
            manifest = call("read_file", path="project.bengine")["structuredContent"]["contents"]
            call("set_main_scene", path="capture.scene", expectedContents=manifest)
            call("run_test", width=320, height=180, frames=4, timeoutSeconds=15,
                 specification="Blue animated cube visible in the game camera; Scene ready logged.")
            deadline = time.monotonic() + 25
            while time.monotonic() < deadline:
                result = call("test_status")
                status = result["structuredContent"]
                if not status["running"]:
                    assert status["exitCode"] == 0 and not status["timedOut"], status
                    assert status["report"]["capturedFrame"] == 4, status
                    assert "Scene ready" in status["log"], status
                    images = [entry for entry in result["content"] if entry["type"] == "image"]
                    assert len(images) == 1, result
                    png = base64.b64decode(images[0]["data"], validate=True)
                    assert png[:8] == b"\x89PNG\r\n\x1a\n"
                    assert struct.unpack(">II", png[16:24]) == (320, 180)
                    assert Path(status["capture"]).read_bytes() == png
                    assert (Path(directory) / "game/project.bengine").exists()
                    print("Created project, model, animation, script and scene; runtime captured frame 4 at 320x180.")
                    return 0
                time.sleep(.1)
            raise RuntimeError("Test did not finish within its deadline")
        finally:
            process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
