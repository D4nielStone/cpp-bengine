"""HTTP security and real stdio tool parity regression coverage."""
import http.client
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest

SERVER = str(Path(sys.argv.pop(1)).resolve())
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("transport", Path(__file__).parents[1] / "apps/mcp/http_transport.py")
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)
TOKEN = "test-credential-" + "x" * 40
INITIALIZE = dict(jsonrpc="2.0", id=1, method="initialize", params=dict(
    protocolVersion=transport.PROTOCOL, capabilities={}, clientInfo=dict(name="test", version="1")))


class HttpTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.server = transport.Transport(0, TOKEN, [SERVER, "--workspace", self.directory.name])
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.sid = None

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.directory.cleanup()

    def request(self, message=None, method="POST", **overrides):
        headers = {"Authorization": "Bearer " + TOKEN, "Accept": "application/json, text/event-stream",
                   "Content-Type": "application/json", "MCP-Protocol-Version": transport.PROTOCOL}
        if self.sid:
            headers["Mcp-Session-Id"] = self.sid
        headers.update(overrides)
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=35)
        connection.request(method, "/mcp", json.dumps(message) if message is not None else None, headers)
        response = connection.getresponse()
        data = response.read()
        result = response.status, dict(response.getheaders()), json.loads(data) if data else None
        connection.close()
        return result

    def initialize(self):
        status, headers, reply = self.request(INITIALIZE)
        self.assertEqual(status, 200)
        self.assertEqual(reply["result"]["protocolVersion"], transport.PROTOCOL)
        self.sid = headers["Mcp-Session-Id"]
        self.assertEqual(self.request(dict(jsonrpc="2.0", method="notifications/initialized"))[0], 202)

    def test_real_discovery_matches_stdio(self):
        self.initialize()
        status, _, reply = self.request(dict(jsonrpc="2.0", id=2, method="tools/list"))
        self.assertEqual(status, 200)
        tools = {t["name"] for t in reply["result"]["tools"]}
        self.assertTrue({"compose_scene", "get_entity", "run_test", "write_asset"} <= tools)
        # Real write and read through HTTP, preserving the existing tool implementation.
        status, _, reply = self.request(dict(jsonrpc="2.0", id=3, method="tools/call",
            params=dict(name="create_project", arguments={"path": "greenhouse", "template": "empty"})))
        self.assertEqual(status, 200)
        self.assertFalse(reply["result"].get("isError"), reply)
        self.assertTrue((Path(self.directory.name) / "greenhouse/project.bengine").exists())
        child = self.server.session.process
        self.assertEqual(self.request(method="DELETE")[0], 204)
        self.assertIsNotNone(child.poll())
        self.assertEqual(self.request(dict(jsonrpc="2.0", id=4, method="ping"))[0], 404)

    def test_subprocess_start_failure_can_retry(self):
        command = self.server.command
        self.server.command = [str(Path(self.directory.name) / "missing-server")]
        self.assertEqual(self.request(INITIALIZE)[0], 502)
        self.assertIsNone(self.server.session)
        self.server.command = command
        self.initialize()

    def test_security_and_http_contract(self):
        self.assertEqual(self.request(INITIALIZE, Authorization="Bearer wrong")[0], 401)
        self.assertEqual(self.request(INITIALIZE, Origin="https://evil.example")[0], 403)
        self.assertEqual(self.request(INITIALIZE, Host="evil.example")[0], 403)
        self.assertEqual(self.request(INITIALIZE, Accept="application/json")[0], 406)
        self.assertEqual(self.request(INITIALIZE, **{"Content-Type": "text/plain"})[0], 415)
        self.assertEqual(self.request(INITIALIZE, **{"Content-Length": str(transport.MAX_BODY + 1)})[0], 413)
        self.assertEqual(self.request([INITIALIZE])[0], 400)
        self.assertEqual(self.request(method="GET")[0], 405)
        self.assertEqual(self.request(dict(jsonrpc="2.0", method="ping", id=1))[0], 400)
        self.initialize()
        self.assertEqual(self.request(dict(jsonrpc="2.0", method="ping", id=2), **{"Mcp-Session-Id": "wrong"})[0], 404)
        self.assertEqual(self.request(dict(jsonrpc="2.0", method="ping", id=2), **{"MCP-Protocol-Version": "invalid"})[0], 400)
        sid = self.sid
        self.sid = None
        self.assertEqual(self.request(INITIALIZE)[0], 409)
        self.sid = sid
        self.server.session.touched -= 301
        self.assertEqual(self.request(dict(jsonrpc="2.0", method="ping", id=3))[0], 404)


if __name__ == "__main__":
    unittest.main()
