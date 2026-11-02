"""Handheld Daemon's local API for tests: fake_hhd.py FIXTURE TOKEN LOG.

Serves GET /api/v1/settings and /api/v1/state from the fixture and applies POST /api/v1/state
(dotted keys, as hhdctl sends them), appending each accepted body to LOG. A missing or wrong
bearer token gets 401, as hhd's does. Prints its port, then serves until killed.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import sys

fixture = json.load(open(sys.argv[1]))
token, log = sys.argv[2], sys.argv[3]
state = fixture["state"]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *arguments):
        pass

    def reply(self, code, body):
        data = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def authorized(self):
        if self.headers.get("Authorization", "") == "Bearer " + token:
            return True
        self.reply(401, {"error": "token"})
        return False

    def do_GET(self):
        if not self.authorized():
            return
        if self.path == "/api/v1/settings":
            self.reply(200, fixture["settings"])
        elif self.path == "/api/v1/state":
            self.reply(200, state)
        else:
            self.reply(404, {})

    def do_POST(self):
        if not self.authorized():
            return
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
        with open(log, "a") as out:
            out.write(json.dumps(body, sort_keys=True) + "\n")
        for key, value in body.items():
            node = state
            *parents, last = key.split(".")
            for part in parents:
                node = node.setdefault(part, {})
            node[last] = value
        self.reply(200, state)


server = HTTPServer(("127.0.0.1", 0), Handler)
print(server.server_port, flush=True)
server.serve_forever()
