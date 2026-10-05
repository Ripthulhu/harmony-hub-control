"""Disposable browser fixture: memory-only setup, no hub connection or IR output."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1] / "payload/www"
configuration = {"schemaVersion": 1, "revision": 1, "setupMode": "keep", "layouts": {}, "activities": [], "deviceSetup": {}}
devices = [{"id": "1", "name": "LG C5", "manufacturer": "LG", "model": "C5", "type": "Television",
            "commands": [{"name": name} for name in ["PowerOn", "PowerOff", "VolumeUp", "VolumeDown", "Mute", "Up", "Down", "Left", "Right", "OK"]]}]


class Preview(BaseHTTPRequestHandler):
    def reply(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        responses = {"/api/v1/session": {"role": "owner", "id": "fixture", "csrf": "fixture"},
                     "/api/v1/configuration": configuration, "/api/v1/devices": {"devices": devices},
                     "/api/v1/bluetooth/devices": {"devices": []}, "/api/v1/operations": {"state": "completed"}}
        if path in responses:
            return self.reply(responses[path])
        name = "index.html" if path == "/" else path[1:]
        if name not in {"index.html", "app.js", "app.css", "profiles.js", "icons.svg"}:
            return self.reply({"error": "Fixture route not implemented"}, 404)
        body = (ROOT / name).read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", {".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml"}[Path(name).suffix])
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        if not 0 < length <= 512000:
            return self.reply({"error": "Invalid request size"}, 400)
        body = json.loads(self.rfile.read(length))
        if self.path == "/api/v1/commands/send":
            return self.reply({"id": "fixture-no-transmission"})
        if body.get("revision") != configuration["revision"]:
            return self.reply({"error": "Configuration changed in another browser. Reload before saving."}, 409)
        if self.path == "/api/v1/devices" and body.get("action") == "create-profile":
            device_id = str(max(int(d["id"]) for d in devices) + 1)
            devices.append(dict(id=device_id, **{k: body[k] for k in ("name", "manufacturer", "model", "type")},
                                commands=[{"name": row.split("|")[0]} for row in body["payload"].splitlines()]))
            configuration["layouts"][device_id] = body["layout"]
            configuration["deviceSetup"][device_id] = {"status": "untested", "source": body["source"], "testedCommand": ""}
            configuration["revision"] += 1
            return self.reply({"ok": True, "deviceId": device_id})
        if self.path == "/api/v1/configuration":
            configuration.update(body["configuration"])
            configuration["revision"] += 1
            return self.reply({"ok": True})
        self.reply({"error": "Fixture action not implemented"}, 400)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8097)
    args = parser.parse_args()
    print(f"Memory-only UI fixture: http://127.0.0.1:{args.port}/", flush=True)
    HTTPServer(("127.0.0.1", args.port), Preview).serve_forever()
