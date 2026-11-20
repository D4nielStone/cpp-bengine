#!/usr/bin/env python3
"""Authenticated loopback Streamable HTTP adapter for the existing stdio server."""
import argparse
import hmac
import json
import os
import queue
import secrets
import signal
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROTOCOL = "2025-11-25"
MAX_BODY = 1024 * 1024
MAX_REPLY = 24 * 1024 * 1024


class Session:
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.messages = queue.Queue(maxsize=2)
        self.identifier = secrets.token_urlsafe(32)
        self.touched = time.monotonic()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            line = self.process.stdout.readline(MAX_REPLY + 1)
            if not line or len(line) > MAX_REPLY:
                self.messages.put(None)
                return
            self.messages.put(line)

    def exchange(self, message):
        self.process.stdin.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode() + b"\n")
        self.process.stdin.flush()
        self.touched = time.monotonic()
        if "id" not in message:
            return None
        reply = self.messages.get(timeout=30)
        if reply is None:
            raise RuntimeError("MCP subprocess ended")
        return json.loads(reply)

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()


class Transport(ThreadingHTTPServer):
    daemon_threads = True
    # One bounded writer session: multiple subprocesses could race on project files.
    def __init__(self, port, token, command, origins=()):
        super().__init__(("127.0.0.1", port), Handler)
        self.token = token
        self.command = command
        self.origins = frozenset(origins)
        self.session = None
        self.lock = threading.Lock()
        self.slots = threading.BoundedSemaphore(8)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        super().process_request(request, address)

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()

    def server_close(self):
        super().server_close()
        with self.lock:
            if self.session:
                self.session.close()
                self.session = None


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(10)

    def log_message(self, *_):
        pass  # Never log Authorization, request bodies, or session credentials.

    def respond(self, status, value=None, headers=None):
        body = b"" if value is None else json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        if value is not None:
            self.send_header("Content-Type", "application/json")
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def authorize(self):
        if self.path != "/mcp":
            self.respond(404)
            return False
        expected_hosts = {f"127.0.0.1:{self.server.server_port}",
                          f"localhost:{self.server.server_port}"}
        if self.headers.get_all("Host") != [self.headers.get("Host")] or self.headers.get("Host") not in expected_hosts:
            self.respond(403)
            return False
        origins = self.headers.get_all("Origin") or []
        if len(origins) > 1 or (origins and origins[0] not in self.server.origins):
            self.respond(403)
            return False
        auth = self.headers.get_all("Authorization") or []
        if len(auth) != 1 or not hmac.compare_digest(auth[0].encode(),
                ("Bearer " + self.server.token).encode()):
            self.respond(401, headers={"WWW-Authenticate": 'Bearer realm="bubble-engine"'})
            return False
        return True

    def do_GET(self):
        if self.authorize():
            self.respond(405, headers={"Allow": "POST, DELETE"})

    def do_DELETE(self):
        if not self.authorize():
            return
        with self.server.lock:
            if not self.valid_session():
                return
            self.server.session.close()
            self.server.session = None
            self.respond(204)

    def valid_session(self):
        session = self.server.session
        if session and time.monotonic() - session.touched > 300:
            session.close()
            self.server.session = session = None
        if not self.headers.get("Mcp-Session-Id"):
            self.respond(400)
        elif not session or not hmac.compare_digest(self.headers["Mcp-Session-Id"].encode(), session.identifier.encode()):
            self.respond(404)
        elif self.headers.get("MCP-Protocol-Version", "2025-03-26") != PROTOCOL:
            self.respond(400)
        else:
            return True
        return False

    def do_POST(self):
        if not self.authorize():
            return
        if self.headers.get_content_type() != "application/json":
            self.respond(415)
            return
        accepts = {item.split(";", 1)[0].strip() for item in self.headers.get("Accept", "").split(",")}
        if not {"application/json", "text/event-stream"} <= accepts:
            self.respond(406)
            return
        lengths = self.headers.get_all("Content-Length") or []
        if self.headers.get("Transfer-Encoding") or len(lengths) != 1 or not lengths[0].isascii() or not lengths[0].isdigit():
            self.respond(400)
            return
        if len(lengths[0]) > 7:
            self.respond(413)
            return
        size = int(lengths[0])
        if size > MAX_BODY:
            self.respond(413)
            return
        try:
            body = self.rfile.read(size)
            if len(body) != size:
                raise ValueError("truncated body")
            message = json.loads(body.decode("utf-8"))
            if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
                raise ValueError("expected JSON-RPC message")
        except (ValueError, RecursionError):
            self.respond(400)
            return
        with self.server.lock:
            initialize = message["method"] == "initialize"
            if initialize:
                if self.headers.get("Mcp-Session-Id") or "id" not in message:
                    self.respond(400)
                    return
                if self.server.session and time.monotonic() - self.server.session.touched > 300:
                    self.server.session.close()
                    self.server.session = None
                if self.server.session:
                    self.respond(409)
                    return
                try:
                    self.server.session = Session(self.server.command)
                except OSError:
                    self.respond(502)
                    return
            elif not self.valid_session():
                return
            try:
                reply = self.server.session.exchange(message)
                headers = {}
                if initialize:
                    if not reply or "error" in reply:
                        self.server.session.close()
                        self.server.session = None
                    else:
                        headers["Mcp-Session-Id"] = self.server.session.identifier
                self.respond(200 if reply is not None else 202, reply, headers)
            except (OSError, RuntimeError, ValueError, queue.Empty):
                if self.server.session:
                    self.server.session.close()
                    self.server.session = None
                self.respond(502)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--origin", action="append", default=[])
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    token = os.environ.get("BENGINE_MCP_TOKEN", "")
    if len(token) < 32 or not token.isascii() or any(c.isspace() for c in token):
        parser.error("BENGINE_MCP_TOKEN must contain at least 32 non-whitespace ASCII characters")
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("expected -- /path/to/bengine_mcp --project ...")
    # Do not pass the bearer credential on to runtime/scripts launched by the engine.
    os.environ.pop("BENGINE_MCP_TOKEN", None)
    def stop(_signum, _frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    with Transport(args.port, token, command, args.origin) as server:
        print(f"Bubble MCP listening on http://127.0.0.1:{server.server_port}/mcp", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
