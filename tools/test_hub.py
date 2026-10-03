"""Owned-hub API checks. Hardware sends require the explicit --volume-test flag."""
import argparse
import http.client
import json
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hub-host", required=True)
    parser.add_argument("--key-path", required=True)
    parser.add_argument("--device-id")
    parser.add_argument("--volume-test", action="store_true")
    args = parser.parse_args()
    key = subprocess.check_output(["ssh", "-i", args.key_path, "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                                   "root@" + args.hub_host, "cat /data/codex/local/internal.key"], text=True).strip()

    def request(path, body=None, authenticated=True, host=None):
        connection = http.client.HTTPConnection(args.hub_host, 8080, timeout=15)
        headers = {"Host": host or args.hub_host + ":8080"}
        if authenticated:
            headers.update({"Authorization": "Bearer " + key, "X-Harmony-CSRF": key})
        connection.request("POST" if body is not None else "GET", "/api/v1/" + path,
                           json.dumps(body) if body is not None else None, headers)
        response = connection.getresponse()
        status, data = response.status, json.loads(response.read())
        connection.close()
        return status, data

    assert request("devices", authenticated=False)[0] == 401
    assert request("devices", host="attacker.example")[0] == 403
    assert request("backups/full")[0] == 403
    assert request("controllers")[0] == 403
    assert request("session", authenticated=False)[0] == 200
    status, inventory = request("devices")
    assert status == 200 and inventory["ok"]
    print(f"Access checks passed; {inventory['deviceCount']} IR devices, {inventory['totalCommandCount']} commands")
    if not args.volume_test:
        return
    device = next(d for d in inventory["devices"] if d["id"] == args.device_id)
    commands = {"".join(c for c in command["name"].lower() if c.isalnum()): command["name"] for command in device["commands"]}
    volume_down = next(commands[name] for name in ("volumedown", "voldown", "voldn") if name in commands)
    volume_up = next(commands[name] for name in ("volumeup", "volup") if name in commands)

    def wait(operation):
        for _ in range(200):
            status, result = request("operations?id=" + operation)
            assert status == 200, result
            if result["state"] in ("completed", "cancelled", "failed"):
                assert result["state"] != "failed", result
                return result
            time.sleep(.05)
        raise AssertionError("Operation did not finish")

    status, operation = request("commands/send", {"deviceId": device["id"], "command": volume_down, "transport": "ir", "mode": "tap"})
    assert status == 202, operation
    result = wait(operation["id"])
    assert result["state"] == "completed", result
    print("Volume-down tap accepted by the native engine")
    status, operation = request("commands/send", {"deviceId": device["id"], "command": volume_up, "transport": "ir", "mode": "hold"})
    assert status == 202, operation
    try:
        for _ in range(3):
            time.sleep(.2)
            assert request("operations", {"id": operation["id"], "action": "keepalive"})[0] == 200
    finally:
        request("operations", {"id": operation["id"], "action": "cancel"})
    result = wait(operation["id"])
    assert result["state"] == "cancelled", result
    print("Volume-up hold renewed, released and cancelled through the shared coordinator")
    # A stale hold must release without a controller sending an explicit cancel.
    status, operation = request("commands/send", {"deviceId": device["id"], "command": volume_down, "transport": "ir", "mode": "hold"})
    assert status == 202, operation
    result = wait(operation["id"])
    print("Lost-controller hold lease ended:", result["state"])


if __name__ == "__main__":
    main()
