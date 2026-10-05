"""Exercise the real Lua bridge with an isolated loopback-only Mosquitto broker."""
import json
from pathlib import Path
import subprocess
import tempfile
import time


def wait(test, timeout=20):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if test():
            return
        time.sleep(0.1)
    raise AssertionError("Timed out waiting for fixture")


def main():
    with tempfile.TemporaryDirectory(prefix="harmony-mqtt-") as directory:
        root = Path(directory)
        config = root / "mosquitto.conf"
        config.write_text("listener 1883\nallow_anonymous true\npersistence false\n")
        config.chmod(0o644)
        container = subprocess.check_output(["docker", "run", "--rm", "-d", "-p", "127.0.0.1::1883",
            "-v", f"{config}:/mosquitto/config/mosquitto.conf:ro", "eclipse-mosquitto:2"], text=True).strip()
        worker = None
        try:
            port = subprocess.check_output(["docker", "port", container, "1883"], text=True).strip().rsplit(":", 1)[1]
            def pub(topic, payload, retain=False):
                subprocess.run(["docker", "exec", container, "mosquitto_pub", "-h", "127.0.0.1", "-t", topic,
                                "-m", payload, *(["-r"] if retain else [])], check=True)
            def sub(topic):
                return subprocess.check_output(["docker", "exec", container, "mosquitto_sub", "-h", "127.0.0.1",
                    "-t", topic, "-C", "1", "-W", "4"], text=True).strip()
            inventory = {"deviceCount": 1, "totalCommandCount": 1, "devices": [{"id": "12", "name": "TV",
                "manufacturer": "Test", "model": "Fixture", "commands": [{"name": "VolumeUp"}]}]}
            (root / "inventory.json").write_text(json.dumps(inventory))
            command = json.dumps({"deviceId": "12", "command": "VolumeUp"})
            pub("test/hub/command", command, True)
            with (root / "worker.log").open("w") as log:
                worker = subprocess.Popen(["lua5.1", "tools/mqtt_fixture.lua", directory, port], stdout=log, stderr=log)
                state = root / "_tmp_harmony-operations_mqtt-status.json"
                wait(lambda: state.exists() and json.loads(state.read_text())["state"] == "connected")
                assert sub("test/hub/status") == "online"
                assert not (root / "commands.jsonl").exists(), "retained command was replayed"
                for _ in range(3):
                    pub("test/hub/command", command)
                commands = root / "commands.jsonl"
                wait(lambda: commands.exists() and len(commands.read_text().splitlines()) == 3)
                topic = "ha/button/fixture_device_3132_566f6c756d655570/config"
                discovery = json.loads(sub(topic))
                assert json.loads(discovery["payload_press"]) == json.loads(command)
                assert discovery["device"]["via_device"] == "fixture"
                # HA birth must republish discovery without executing a command.
                pub("ha/status", "online")
                assert json.loads(sub("ha/select/fixture_activity/config"))["options"] == ["PowerOff"]
                time.sleep(6)  # Cross the keepalive/PINGRESP boundary.
                assert worker.poll() is None
                assert json.loads(state.read_text())["state"] == "connected"
                subprocess.run(["docker", "pause", container], check=True, stdout=subprocess.DEVNULL)
                wait(lambda: json.loads(state.read_text())["state"] == "disconnected", timeout=15)
                assert "PINGRESP" in json.loads(state.read_text())["error"]
                subprocess.run(["docker", "unpause", container], check=True, stdout=subprocess.DEVNULL)
                wait(lambda: json.loads(state.read_text())["state"] == "connected", timeout=35)
                assert len(commands.read_text().splitlines()) == 3, "reconnect replayed retained command"
                inventory["devices"][0]["commands"] = []
                inventory["totalCommandCount"] = 0
                (root / "inventory.json").write_text(json.dumps(inventory))
                manifest = root / "_data_codexmqtt_discovery.json"
                wait(lambda: topic not in json.loads(manifest.read_text())["topics"])
                removed = subprocess.run(["docker", "exec", container, "mosquitto_sub", "-h", "127.0.0.1",
                    "-t", topic, "-C", "1", "-W", "1"], capture_output=True, text=True)
                assert removed.returncode == 27 and not removed.stdout, "deleted command discovery is still retained"
                # Abrupt client termination must make the retained availability offline.
                worker.kill(); worker.wait(timeout=5); worker = None
                wait(lambda: sub("test/hub/status") == "offline")
            print("Mosquitto: discovery, retained-command rejection, rapid taps, HA birth, missed PINGRESP, reconnect, removal and last will passed")
        finally:
            if worker:
                worker.terminate(); worker.wait(timeout=5)
            subprocess.run(["docker", "unpause", container], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.run(["docker", "stop", container], check=True, stdout=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
