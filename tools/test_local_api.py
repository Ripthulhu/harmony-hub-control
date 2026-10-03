"""Disposable Linux integration tests; never connect to a physical hub."""
import hashlib
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
import fcntl

ROOT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix="harmony-local-") as directory:
        base = Path(directory)
        local, ops, resources, www = [base / n for n in ("local", "operations", "resources", "www")]
        for path in (local, ops, resources, www):
            path.mkdir()
        button_path = base / "input-events"
        os.mkfifo(button_path)
        button_fd = os.open(button_path, os.O_RDWR | os.O_NONBLOCK)
        fixtures = {"DeviceList": {"DevicesWithFeatures": []}, "FunctionList": {"FunctionMaps": []},
                    "ProtocolList": {"Protocols": []}, "ActivityList": {"Activities": []}, "MapList": {"ButtonMaps": []}}
        for name, value in fixtures.items():
            (resources / (name + ".json")).write_text(json.dumps(value))
        (base / "hub_id").write_text("19111956")
        (www / "index.html").write_text("<!doctype html><title>test</title>")
        defs = {"LOCAL_ROOT": local, "LOCAL_OPS": ops, "LOCAL_WWW": www, "LOCAL_BUTTON_DEVICE": button_path,
                "DEVICE_LIST": resources / "DeviceList.json", "FUNCTION_LIST": resources / "FunctionList.json",
                "PROTOCOL_LIST": resources / "ProtocolList.json", "ACTIVITY_LIST": resources / "ActivityList.json",
                "MAP_LIST": resources / "MapList.json", "BT_DEVICE_STORE": resources / "bt-devices.json",
                "MQTT_CONFIG": base / "mqtt.json", "WPA_CONFIG": base / "wifi.conf", "HUB_ID_FILE": base / "hub_id",
                "RESOURCE_RELOAD_FLAG": base / "reload", "CODEX_BIN_DIR": base / "absent-helpers",
                "IR_SEND_LOCK": base / "ir.lock",
                "LOCAL_UPDATE_STAGE": base / "update", "LOCAL_UPDATE_BACKUP": base / "rollback", "LOCAL_UPDATE_VOLUME": base,
                "LOCAL_CORE_FILE": base / "localcore.lua", "LOCAL_MQTT_FILE": base / "codexmqtt.lua",
                "LOCAL_INIT_FILE": base / "init.sh", "LOCAL_MAINTENANCE_FILE": base / "maintenance.sh"}
        source = ROOT / "payload/source"
        binary = base / "webui"
        subprocess.run([os.environ.get("CC", "gcc"), "-Os", "-DCJSON_NESTING_LIMIT=32", "-DLOCAL_TEST_NO_REBOOT=1"] +
                       [f'-D{k}="{v}"' for k, v in defs.items()] +
                       [str(source / p) for p in ("codex_webui.c", "vendor/cJSON.c", "vendor/monocypher.c", "vendor/monocypher-ed25519.c")] +
                       ["-o", str(binary)], check=True)
        code = subprocess.check_output([binary, "--claim-code"], text=True).strip()
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0)); port = sock.getsockname()[1]
        log = (base / "log").open("w")
        server = subprocess.Popen([binary, str(port)], stdout=log, stderr=log, start_new_session=True)
        coordinator = None

        def request(path, body=None, cookie="", csrf="", extra=None, raw=None):
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
            headers = {"Host": f"127.0.0.1:{port}", "Cookie": cookie, "X-Harmony-CSRF": csrf}
            headers.update(extra or {})
            text = raw if raw is not None else json.dumps(body) if body is not None else None
            conn.request("POST" if text is not None else "GET", "/api/v1/" + path, text, headers)
            response = conn.getresponse()
            data = response.read(); result = json.loads(data)
            pair_cookie = response.getheader("Set-Cookie", "").split(";")[0]
            status = response.status; conn.close()
            return status, result, pair_cookie

        def button_event(value, code=30, stamp=None, event_type=1):
            stamp = time.time() if stamp is None else stamp
            os.write(button_fd, struct.pack("llHHi", int(stamp), int(stamp % 1 * 1000000), event_type, code, value))
            time.sleep(.03)

        def press(duration=.15):
            stamp = time.time()
            button_event(1, stamp=stamp)
            button_event(0, stamp=stamp + duration)

        def role(cookie):
            return request("session", cookie=cookie)[1]["role"]

        try:
            for _ in range(100):
                try:
                    if request("session")[0] == 200: break
                except OSError: time.sleep(.05)
            else: raise AssertionError("server failed to start")
            assert request("devices")[0] == 401
            assert request("session", extra={"Host": "attacker.example"})[0] == 403
            assert request("session")[1]["buttonAvailable"]
            status, first, first_cookie = request("controllers/request", {"name": "Physical owner", "button": True})
            assert status == 200 and role(first_cookie) == "pending"
            assert not request("session")[1]["claimed"]
            press()
            assert role(first_cookie) == "owner"
            assert not (local / "claim.hash").exists()
            # Only disposable state: reset the test registry to exercise code claiming too.
            (local / "controllers.json").unlink()
            code = subprocess.check_output([binary, "--claim-code"], text=True).strip()
            status, owner, cookie = request("controllers/claim", {"name": "Owner", "code": code})
            assert status == 200 and owner["role"] == "owner"
            csrf = owner["csrf"]
            status, physical, physical_cookie = request("controllers/request", {"name": "Button phone", "button": True})
            assert status == 200 and role(physical_cookie) == "pending"
            assert request("controllers/request", {"name": "Competing phone", "button": True})[0] == 409
            assert request("controllers/cancel", {}, physical_cookie, "wrong")[0] == 403
            button_event(0)  # Release without a fresh press.
            button_event(1, code=31); button_event(0, code=31)
            button_event(2); button_event(0)  # Autorepeat is not a fresh press.
            old = time.time() - 10
            button_event(1, stamp=old); button_event(0, stamp=old + .15)
            press(3)  # Long holds must never approve.
            assert role(physical_cookie) == "pending"
            button_event(1); button_event(0, code=3, event_type=0)  # SYN_DROPPED
            button_event(1); button_event(0); button_event(0, code=0, event_type=0)
            assert role(physical_cookie) == "pending"
            window_path = ops / "pair-window"
            window = json.loads(window_path.read_text()); window["created"] -= 90000
            window_path.write_text(json.dumps(window))
            press(); assert role(physical_cookie) == "pending"
            assert request("controllers/cancel", {}, physical_cookie, physical["csrf"])[0] == 200
            assert role(physical_cookie) == "unpaired"
            status, physical, physical_cookie = request("controllers/request", {"name": "Button phone", "button": True})
            assert status == 200
            button_event(1)
            assert request("controllers/cancel", {}, physical_cookie, physical["csrf"])[0] == 200
            status, physical, physical_cookie = request("controllers/request", {"name": "Replacement phone", "button": True})
            button_event(0, stamp=time.time() + .15)
            assert role(physical_cookie) == "pending", "an old press must not approve a new request"
            press()
            assert role(physical_cookie) == "owner"
            assert request("controllers", cookie=physical_cookie)[0] == 200
            assert request("backups/full", cookie=physical_cookie)[0] == 200
            assert request("controllers", {"id": physical["id"], "action": "revoke"}, cookie, csrf)[0] == 200
            press(); assert role(physical_cookie) == "unpaired"
            print("Physical pairing: full ownership, one request, expiry, cancel, stale/long/wrong/dropped events and revocation passed")
            assert request("controllers/claim", {"name": "Second", "code": code})[0] == 409
            status, pending, second = request("controllers/request", {"name": "Phone"})
            assert status == 200 and pending["role"] == "pending"
            assert request("devices", cookie=second)[0] == 401
            assert request("controllers", {"id": pending["id"], "action": "approve"}, cookie, csrf)[0] == 200
            assert request("devices", cookie=second)[0] == 200
            assert request("backups/full", cookie=second)[0] == 403
            # Existing control-only browsers can gain ownership only after a fresh physical press.
            status, upgrade, upgrade_cookie = request("controllers/request", {"name": "Upgradeable"})
            assert request("controllers", {"id": upgrade["id"], "action": "approve"}, cookie, csrf)[0] == 200
            assert request("controllers/upgrade", {}, upgrade_cookie, "wrong")[0] == 403
            assert request("controllers/upgrade", {}, upgrade_cookie, upgrade["csrf"])[0] == 200
            assert role(upgrade_cookie) == "control" and request("session", cookie=upgrade_cookie)[1]["buttonPending"]
            assert request("controllers/cancel", {}, upgrade_cookie, upgrade["csrf"])[0] == 200
            press(); assert role(upgrade_cookie) == "control"
            assert request("controllers/upgrade", {}, upgrade_cookie, upgrade["csrf"])[0] == 200
            press(); assert role(upgrade_cookie) == "owner"
            assert request("controllers", cookie=upgrade_cookie)[0] == 200
            assert request("controllers", {"id": upgrade["id"], "action": "revoke"}, cookie, csrf)[0] == 200
            assert request("setup", {"revision": 1, "mode": "keep"}, cookie)[0] == 403
            assert request("setup", {"revision": 1, "mode": "keep"}, cookie, csrf, {"Origin": "http://attacker.example"})[0] == 403
            assert request("setup", cookie=cookie, csrf=csrf, raw='{"revision":1,"revision":1}')[0] == 400
            assert request("setup", {"revision": 1, "mode": "keep"}, cookie, csrf)[0] == 200
            assert request("setup", {"revision": 1, "mode": "keep"}, cookie, csrf)[0] == 409
            assert request("configuration", cookie=cookie)[1]["revision"] == 2
            backup = request("backups/portable", cookie=cookie)[1]
            assert request("backups/restore", {"revision": 2, "backup": backup}, cookie, csrf)[0] == 200
            assert request("configuration", cookie=cookie)[1]["revision"] == 3
            with (base / "ir.lock").open("w") as held:
                fcntl.flock(held, fcntl.LOCK_EX)
                assert request("setup", {"revision": 3, "mode": "keep"}, cookie, csrf)[0] == 409
            configuration = request("configuration", cookie=cookie)[1]
            broken_revision = dict(configuration); broken_revision["revision"] = 3.5
            assert request("configuration", {"revision": 3, "configuration": broken_revision}, cookie, csrf)[0] == 400
            assert request("configuration", cookie=cookie)[1]["revision"] == 3
            invalid = dict(backup); invalid["resources"] = {"DeviceList.json": []}
            assert request("backups/restore", {"revision": 3, "backup": invalid}, cookie, csrf)[0] == 400
            assert request("configuration", cookie=cookie)[1]["revision"] == 3
            # Queue cancellation and revocation cannot be bypassed by another controller.
            status, created, _ = request("devices", {"revision": 3, "action": "create", "transport": "ir", "name": "Test TV", "manufacturer": "LG", "model": "Test", "type": "Television"}, cookie, csrf)
            assert status == 200, created
            device_id = created["deviceId"]
            assert request("commands/save", {"revision": 4, "deviceId": device_id, "name": "PowerOff", "mode": "nec", "protocol": "2", "nec": "20DFA35C"}, cookie, csrf)[0] == 200
            assert request("commands/import", {"revision": 5, "deviceId": device_id, "payload": "VolumeUp|raw|F9470P100S100P100S100"}, cookie, csrf)[0] == 200
            assert request("devices", cookie=cookie)[1]["devices"][0]["commands"][0]["name"] == "PowerOff"
            configuration = request("configuration", cookie=cookie)[1]
            configuration["layouts"][device_id] = [{"slot": "volumeup", "label": "Volume", "command": "VolumeUp"}]
            configuration["activities"] = [{"id": "watch", "name": "Watch TV", "steps": [], "exitSteps": [], "buttons": [{"slot": "volumeup", "deviceId": device_id, "command": "VolumeUp", "label": "Volume"}]}]
            assert request("configuration", {"revision": 6, "configuration": configuration}, cookie, csrf)[0] == 200
            assert request("commands/save", {"revision": 7, "deviceId": device_id, "oldName": "PowerOff", "name": "Off", "mode": "nec", "protocol": "2", "nec": "20DFA35C"}, cookie, csrf)[0] == 200
            assert request("devices", {"revision": 8, "deviceId": device_id, "action": "delete"}, cookie, csrf)[0] == 200
            assert request("devices", cookie=cookie)[1]["devices"] == []
            assert request("backups/restore", {"revision": 9, "backup": backup}, cookie, csrf)[0] == 200
            command = {"deviceId": "123", "command": "VolumeUp", "mode": "hold", "transport": "ir"}
            status, op, _ = request("commands/send", command, second, pending["csrf"])
            assert status == 202
            assert request("operations", {"id": op["id"], "action": "cancel"}, cookie, csrf)[0] == 200
            assert request("controllers", {"id": pending["id"], "action": "revoke"}, cookie, csrf)[0] == 200
            assert request("commands/send", command, second, pending["csrf"])[0] == 401
            coordinator = subprocess.Popen([binary, "--coordinator"], stdout=log, stderr=log, start_new_session=True)
            for _ in range(100):
                result = request("operations?id=" + op["id"], cookie=cookie)[1]
                if result["state"] == "cancelled": break
                time.sleep(.05)
            else: raise AssertionError("revoked operation did not cancel")
            # Valid signature accepted; changed signature/platform/manifest rejected before staging.
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
            from cryptography.hazmat.primitives import serialization
            key = Ed25519PrivateKey.generate()
            (local / "release.pub").write_text(key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex())
            names = ["codex_webui", "codex_hbus", "codex_hal_ltcp", "codex_bthid_keyboard", "codex_portal", "localcore.lua", "codexmqtt.lua", "index.html", "app.css", "app.js", "icons.svg", "profiles.js", "init.sh", "maintenance.sh", "migrations.json"]
            manifest = json.dumps({"schemaVersion": 1, "platform": "pimento-4.15.600", "version": "test-1", "files": [{"name": n, "size": 1, "sha512": hashlib.sha512(b"x").hexdigest()} for n in names]}, separators=(",", ":"))
            signature = key.sign(manifest.encode()).hex()
            assert request("updates/begin", {"manifest": manifest, "signature": "00" * 64}, cookie, csrf)[0] == 403
            assert request("updates/begin", {"manifest": manifest + " ", "signature": signature}, cookie, csrf)[0] == 403
            assert request("updates/begin", {"manifest": manifest, "signature": signature}, cookie, csrf)[0] == 200
            assert request("updates/chunk", {"name": "../../escape", "offset": 0, "hex": "78"}, cookie, csrf)[0] == 400
            assert request("updates/chunk", {"name": "app.js", "offset": 5, "hex": "78"}, cookie, csrf)[0] == 409
            assert request("updates/apply", {}, cookie, csrf)[0] == 403
            contents = {name: b"signed test asset" for name in names}
            contents["migrations.json"] = b'{"from":1,"to":1}'
            release = json.dumps({"schemaVersion": 1, "platform": "pimento-4.15.600", "version": "test-2", "files": [{"name": n, "size": len(raw), "sha512": hashlib.sha512(raw).hexdigest()} for n, raw in contents.items()]}, separators=(",", ":"))
            assert request("updates/begin", {"manifest": release, "signature": key.sign(release.encode()).hex()}, cookie, csrf)[0] == 200
            installed = {}
            for name, raw in contents.items():
                assert request("updates/chunk", {"name": name, "offset": 0, "hex": raw.hex()}, cookie, csrf)[0] == 200
                path = defs["CODEX_BIN_DIR"] / name if name.startswith("codex_") and name != "codexmqtt.lua" else www / name if name in ["index.html", "app.css", "app.js", "icons.svg", "profiles.js"] else local / name if name == "migrations.json" else base / name
                path.parent.mkdir(exist_ok=True); path.write_bytes(b"previous version")
                installed[path] = b"previous version"
            (defs["LOCAL_UPDATE_STAGE"] / "app.js").write_bytes(b"tampered")
            assert request("updates/apply", {}, cookie, csrf)[0] == 403
            (defs["LOCAL_UPDATE_STAGE"] / "app.js").write_bytes(contents["app.js"])
            assert request("updates/apply", {}, cookie, csrf)[0] == 200
            assert all(path.read_bytes() != raw for path, raw in installed.items())
            subprocess.run([binary, "--rollback"], check=True)
            assert all(path.read_bytes() == raw for path, raw in installed.items())
            assert not (local / "update.pending").exists()
            # Duplicate framing, embedded NUL and oversize requests are bounded.
            for headers, body in [("Content-Length: 2\r\nContent-Length: 2\r\n", b"{}"), ("Content-Length: 3\r\n", b"{\0}"), ("Transfer-Encoding: chunked\r\n", b"0\r\n\r\n")]:
                with socket.create_connection(("127.0.0.1", port)) as sock:
                    sock.sendall((f"POST /api/v1/setup HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n" + headers + "\r\n").encode() + body)
                    assert not sock.recv(1024)
            with socket.create_connection(("127.0.0.1", port)) as sock:
                sock.sendall((f"POST /api/v1/setup HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nContent-Length: 524289\r\n\r\n").encode())
                assert b"413 Payload Too Large" in sock.recv(4096)
            # Simulate a crash after native files changed but before transaction commit.
            journal = local / "transaction"
            for i, path in enumerate([local / "config.json", resources / "DeviceList.json", resources / "FunctionList.json", resources / "ProtocolList.json", resources / "bt-devices.json", resources / "ActivityList.json", resources / "MapList.json"]):
                if path.exists(): (journal / str(i)).write_bytes(path.read_bytes())
                else: (journal / (str(i) + ".absent")).write_text("")
            (journal / "pending").write_text("1")
            (resources / "DeviceList.json").write_text("broken")
            subprocess.run([binary, "--internal-key"], check=True)
            assert json.loads((resources / "DeviceList.json").read_text()) == fixtures["DeviceList"]
            assert not (journal / "pending").exists()
            print("Local API: pairing, approval/revocation, ownership, origin/CSRF, revisions, backups, cancellation, signature/framing validation and crash recovery passed")
        finally:
            for process in (coordinator, server):
                if process:
                    os.killpg(process.pid, signal.SIGTERM); process.wait(timeout=5)
            log.close()
            os.close(button_fd)


if __name__ == "__main__":
    run()
