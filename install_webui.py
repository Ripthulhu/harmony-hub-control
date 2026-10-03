#!/usr/bin/env python3
"""
Linux/macOS installer for the Harmony Hub Control post-root web UI.

The hub must already have root SSH access. This installer intentionally uses
plain ssh plus remote "cat > file" uploads, because the minimal Dropbear setup
used by the root tool does not provide scp, sftp, or tftp.
"""

from __future__ import annotations

import argparse
import getpass
import hashlib
import json
import os
import shlex
import socket
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PAYLOAD = ROOT / "payload"


def step(text: str) -> None:
    print(f"\n== {text} ==")


def info(text: str) -> None:
    print(f"  {text}")


def fail(message: str) -> None:
    print(f"\nERROR: {message}", file=sys.stderr)
    sys.exit(1)


def prompt_if_missing(value: str | None, label: str, required: bool, no_prompt: bool) -> str:
    if value:
        return value
    if no_prompt:
        if required:
            raise RuntimeError(f"{label} is required")
        return ""
    entered = input(f"{label}: ").strip()
    if required and not entered:
        raise RuntimeError(f"{label} is required")
    return entered


def resolve_default_key_path() -> Path | None:
    ssh_dir = Path.home() / ".ssh"
    if not ssh_dir.is_dir():
        return None
    keys = [
        p
        for p in ssh_dir.glob("harmony_owner_*")
        if p.is_file() and not p.name.endswith(".pub")
    ]
    if not keys:
        return None
    return sorted(keys, key=lambda p: p.stat().st_mtime, reverse=True)[0]


def valid_hub_id(value: str) -> bool:
    return value.isdigit() and len(value) >= 4


def read_json_file(path: Path) -> object | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def resolve_saved_hub_id(hub_host: str) -> tuple[str, Path] | None:
    candidate_paths: list[Path] = []
    home = Path.home()
    candidate_paths.extend(
        [
            home / ".harmony-hub" / "known_hubs.json",
            home / ".harmony-hub" / "last_root.json",
            home / ".harmony-hub" / "hub_id.txt",
            ROOT / "harmony_hub_id.txt",
        ]
    )

    seen: set[Path] = set()
    for path in candidate_paths:
        if path in seen:
            continue
        seen.add(path)
        if not path.is_file():
            continue
        if path.name == "known_hubs.json":
            known = read_json_file(path)
            if isinstance(known, dict):
                entry = known.get(hub_host)
                if isinstance(entry, dict):
                    hub_id = str(entry.get("hub_id", "")).strip()
                    if valid_hub_id(hub_id):
                        return hub_id, path
            continue
        if path.name == "last_root.json":
            last = read_json_file(path)
            if isinstance(last, dict) and str(last.get("host", "")) == hub_host:
                hub_id = str(last.get("hub_id", "")).strip()
                if valid_hub_id(hub_id):
                    return hub_id, path
            continue
        try:
            hub_id = path.read_text(encoding="utf-8").strip()
        except OSError:
            continue
        if valid_hub_id(hub_id):
            return hub_id, path
    return None


def remote_quote(value: str) -> str:
    return shlex.quote(value)


def split_remote_dir(path: str) -> str:
    parent = path.rsplit("/", 1)[0]
    return parent if parent else "/"


def local_md5(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def tcp_open(host: str, port: int, timeout: float = 1.2) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def wait_for_port(host: str, port: int, seconds: int, label: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if tcp_open(host, port):
            info(f"{label} is reachable on port {port}")
            return
        time.sleep(2)
    raise RuntimeError(f"{label} did not become reachable on port {port} within {seconds} seconds")


class Installer:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.key_path = Path(args.key_path).expanduser().resolve()

    def ssh_base_args(self) -> list[str]:
        return [
            "ssh",
            "-p",
            str(self.args.port),
            "-i",
            str(self.key_path),
            "-o",
            "IdentitiesOnly=yes",
            "-o",
            "BatchMode=yes",
            "-o",
            "StrictHostKeyChecking=accept-new",
            f"{self.args.ssh_user}@{self.args.hub_host}",
        ]

    def run_remote(
        self,
        command: str,
        input_bytes: bytes | None = None,
        timeout: int = 90,
        quiet: bool = False,
    ) -> str:
        proc = subprocess.run(
            self.ssh_base_args() + [command],
            input=input_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            check=False,
        )
        stdout = proc.stdout.decode("utf-8", "replace")
        stderr = proc.stderr.decode("utf-8", "replace")
        if proc.returncode != 0:
            raise RuntimeError(
                "ssh failed with exit {code}\ncommand={cmd}\nstdout={out}\nstderr={err}".format(
                    code=proc.returncode,
                    cmd=command,
                    out=stdout,
                    err=stderr,
                )
            )
        if stderr.strip() and not quiet:
            print(stderr.strip())
        return stdout

    def upload_bytes(self, local_path: Path, remote_path: str, mode: str) -> None:
        self.upload_data(local_path.read_bytes(), remote_path, mode)

    def upload_text(self, text: str, remote_path: str, mode: str) -> None:
        self.upload_data(text.encode("utf-8"), remote_path, mode)

    def upload_data(self, data: bytes, remote_path: str, mode: str) -> None:
        if getattr(self, "preflight", None):
            volume = "/cache" if remote_path.startswith("/cache/") else "/data"
            self.run_remote(f"{self.preflight} --space {volume} {len(data)}", quiet=True)
        remote_dir = split_remote_dir(remote_path)
        tmp = f"{remote_path}.tmp-handoff-{os.getpid()}-{int(time.time())}"
        command = (
            f"mkdir -p {remote_quote(remote_dir)} && "
            f"cat > {remote_quote(tmp)} && "
            f"mv {remote_quote(tmp)} {remote_quote(remote_path)} && "
            f"chmod {mode} {remote_quote(remote_path)}"
        )
        self.run_remote(command, data, timeout=max(90, 45 + len(data) // 12000), quiet=True)
        secret = any(token in remote_path.lower() for token in ("pass", "authorized_keys", "config.json"))
        checksum = "<hidden>" if secret else hashlib.md5(data).hexdigest()
        info(f"{remote_path} bytes={len(data)} md5={checksum}")

    def build_mqtt_config(self) -> str:
        broker = self.args.mqtt_broker or ""
        if broker:
            broker = socket.gethostbyname(broker)
        enabled = bool(broker) and not self.args.mqtt_disabled
        cfg = {
            "enabled": enabled,
            "name": "Harmony Hub",
            "clientId": self.args.mqtt_client_id,
            "baseTopic": self.args.mqtt_base_topic.strip("/"),
            "discoveryPrefix": self.args.mqtt_discovery_prefix.strip("/"),
            "haDiscovery": True,
            "pollSeconds": 10,
            "keepAlive": 60,
            "broker": {
                "host": broker,
                "port": self.args.mqtt_port,
                "username": self.args.mqtt_user or "",
                "password": self.args.mqtt_password or "",
            },
        }
        return json.dumps(cfg, separators=(",", ":")) + "\n"

    def read_remote_bytes(self, path: str, limit: int = 2 * 1024 * 1024) -> bytes:
        proc = subprocess.run(self.ssh_base_args() + [f"cat {remote_quote(path)}"],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=45)
        if proc.returncode or len(proc.stdout) > limit:
            raise RuntimeError(f"Cannot back up {path}; installation stopped")
        return proc.stdout

    def backup(self) -> Path:
        import zipfile
        inventory = json.loads(self.run_remote(self.preflight + " --inventory", quiet=True))
        if not inventory.get("complete"):
            raise RuntimeError("Backup inventory is incomplete; no installed files were changed")
        directory = Path(self.args.backup_dir).expanduser() if self.args.backup_dir else Path.home() / ".harmony-hub" / "backups"
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / (time.strftime("%Y%m%d-%H%M%S") + "-harmony.zip")
        records = []
        seen = set()
        self.originals = {}
        with zipfile.ZipFile(path, "x", compression=zipfile.ZIP_DEFLATED) as archive:
            os.chmod(path, 0o600)
            for record in inventory["files"]:
                remote = record["path"]
                if remote in seen:
                    continue
                seen.add(remote)
                if not remote.startswith(("/data/", "/pkg/", "/etc/", "/opt/", "/usr/", "/home/")) or ".." in remote.split("/"):
                    raise RuntimeError("Unexpected backup path")
                raw = self.read_remote_bytes(remote)
                if len(raw) != record["size"]:
                    raise RuntimeError(f"{remote} changed during backup; try again")
                record["sha256"] = hashlib.sha256(raw).hexdigest()
                archive.writestr(remote.lstrip("/"), raw)
                records.append(record)
            archive.writestr("backup-manifest.json", json.dumps({"schemaVersion": 1, "host": self.args.hub_host, "files": records}, indent=2))
        # Verify the archive before changing the installation.
        with zipfile.ZipFile(path) as archive:
            if archive.testzip():
                raise RuntimeError("Backup verification failed")
            for record in records:
                raw = archive.read(record["path"].lstrip("/"))
                if hashlib.sha256(raw).hexdigest() != record["sha256"]:
                    raise RuntimeError("Backup hash verification failed")
                self.originals[record["path"]] = (raw, format(record["mode"] & 0o777, "o"))
        info(f"Sensitive off-device backup: {path}")
        return path

    def run(self) -> None:
        from tools.restrict_stock import loopback_transport
        step("Checking the supported hub")
        firmware = self.run_remote("cat /etc/version", timeout=30).strip()
        if "4.15.600" not in firmware or self.run_remote("uname -m", timeout=30).strip() != "mips":
            raise RuntimeError("This release is tested only on Harmony Hub / 4.15.600")
        hub_id = self.args.hub_id or self.run_remote("cat /data/codex/hub_id 2>/dev/null || true").strip()
        if not hub_id:
            saved = resolve_saved_hub_id(self.args.hub_host)
            hub_id = saved[0] if saved else ""
        if not valid_hub_id(hub_id):
            raise RuntimeError("A numeric Hub ID from the LAN root tool is required")

        # Only this temporary, read-only helper is uploaded before the backup.
        binary = PAYLOAD / "bin" / "codex_webui"
        if binary.read_bytes()[:6] != b"\x7fELF\x01\x02":
            raise RuntimeError("Expected the big-endian MIPS build, not a desktop test binary")
        self.preflight = None
        self.upload_bytes(binary, "/tmp/harmony-preflight", "755")
        self.preflight = "/tmp/harmony-preflight"
        backup = self.backup()
        if self.args.backup_only:
            info("Backup verified; the running installation is unchanged")
            return

        transports = {}
        for name in ("hbushttpserverconnector.lua", "xmppserverconnector.lua"):
            remote = "/opt/luaworks/tasks/connectserver/transport/" + name
            transports[remote] = loopback_transport(self.read_remote_bytes(remote), name)
        files = {}
        for name in ("dropbearmulti", "codex_dhcpd", "codex_hbus", "codex_hal_ltcp", "codex_bthid_keyboard", "codex_portal", "codex_webui"):
            files["/data/codex/bin/" + name] = ((PAYLOAD / "bin" / name).read_bytes(), "755")
        # A known-good recovery binary is not replaced by browser release activation.
        files["/cache/harmony-recovery"] = (binary.read_bytes(), "755")
        for name in ("init.sh", "maintenance.sh", "recovery_ap.sh", "release_recovery.sh"):
            files["/data/codex/" + name] = ((PAYLOAD / "scripts" / name).read_bytes(), "755")
        for name in ("dropbear", "dropbearkey"):
            raw = (PAYLOAD / "scripts" / name).read_bytes()
            if name == "dropbear":
                raw = raw.replace(b' -R ', b' -s -g -K 300 -R ')
            files["/usr/sbin/" + name] = (raw, "755")
        files["/etc/init.d/rcS.local"] = ((PAYLOAD / "scripts" / "rcS.local").read_bytes(), "755")
        if not self.args.skip_cloud_suppression:
            files["/opt/luaworks/tasks/connectserver/netservicestarter.lua"] = ((PAYLOAD / "scripts" / "netservicestarter.lua").read_bytes(), "644")
        files["/opt/luaworks/tasks/codex/localcore.lua"] = ((PAYLOAD / "core" / "localcore.lua").read_bytes(), "644")
        files["/pkg/codexmqtt/codexmqtt.lua"] = ((PAYLOAD / "mqtt" / "codexmqtt.lua").read_bytes(), "644")
        files["/pkg/codexmqtt/manifest.json"] = (b'{"plugin":"codexmqtt"}\n', "644")
        for name in ("index.html", "app.js", "app.css", "icons.svg", "profiles.js"):
            files["/data/codex/www/" + name] = ((PAYLOAD / "www" / name).read_bytes(), "644")
        files["/data/codex/local/migrations.json"] = ((PAYLOAD / "core" / "migrations.json").read_bytes(), "600")
        files["/data/codex/hub_id"] = ((hub_id + "\n").encode(), "644")
        files["/data/codex/cloud_blocker.conf"] = (b"0\n" if self.args.skip_cloud_suppression else b"1\n", "644")
        files["/etc/tdeenable"] = (b"1\n", "644")
        files.update({path: (data, "644") for path, data in transports.items()})
        if self.args.mqtt_broker or self.args.mqtt_disabled or "/data/codexmqtt/config.json" not in self.originals:
            files["/data/codexmqtt/config.json"] = (self.build_mqtt_config().encode(), "600")
        if self.args.release_public_key:
            public = Path(self.args.release_public_key).read_text().strip()
            if len(public) != 64 or len(bytes.fromhex(public)) != 32:
                raise RuntimeError("Release public key must be 32-byte hexadecimal Ed25519")
            files["/data/codex/local/release.pub"] = (public.encode(), "600")
        if self.args.experimental_blank_bootstrap:
            self.run_remote(self.preflight + " --space /data 16384", quiet=True)
            files["/data/codex/local/bootstrap.requested"] = (b"1", "600")

        changed = []
        step("Installing the local runtime")
        try:
            for remote, (raw, mode) in files.items():
                if self.originals.get(remote) == (raw, mode):
                    continue
                changed.append(remote)
                self.upload_data(raw, remote, mode)
                checksum = self.run_remote(f"md5sum {remote_quote(remote)}", quiet=True).split()[0]
                if checksum != hashlib.md5(raw).hexdigest():
                    raise RuntimeError(f"Upload verification failed for {remote}")
            self.run_remote("mkdir -p /data/codex/local /data/codexmqtt /etc/dropbear /home/root/.ssh && chmod 700 /data/codex/local && ln -sf dropbearmulti /data/codex/bin/dropbear && ln -sf dropbearmulti /data/codex/bin/dropbearkey && /data/codex/bin/codex_webui --internal-key && /tmp/harmony-preflight --sync", quiet=True)
            claim_code = self.run_remote("/data/codex/bin/codex_webui --claim-code", quiet=True).strip()
        except Exception:
            step("Restoring the previous installation")
            for remote in reversed(changed):
                if remote in self.originals:
                    raw, mode = self.originals[remote]
                    self.upload_data(raw, remote, mode)
                else:
                    self.run_remote("rm -f " + remote_quote(remote), quiet=True)
            raise

        if self.args.no_apply_cloud_restart:
            info("Installation staged. Reboot is required to apply the listener restrictions and local service.")
        else:
            step("Restarting the hub")
            self.run_remote("(sleep 2; /sbin/reboot) >/dev/null 2>&1 & echo restarting", quiet=True)
            time.sleep(8)
            wait_for_port(self.args.hub_host, self.args.port, 180, "Owner SSH")
            wait_for_port(self.args.hub_host, 8080, 180, "Local UI")
            deadline = time.monotonic() + 90
            while True:
                try:
                    self.run_remote("/data/codex/bin/codex_webui --health", timeout=15, quiet=True)
                    break
                except RuntimeError:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("The local service did not become healthy; owner SSH and the verified backup remain available")
                    time.sleep(2)
            if tcp_open(self.args.hub_host, 5222) or tcp_open(self.args.hub_host, 8088):
                raise RuntimeError("An old LAN listener is still exposed; recover using the off-device backup")
        step("Installed")
        info(f"Local UI: http://{self.args.hub_host}:8080/")
        if claim_code:
            info(f"Single-use ownership code: {claim_code}")
        info(f"Recovery backup: {backup}")
        info("Trusted LAN HTTP only. Blank-hub setup and the offline soak are not yet verified.")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Install Harmony Hub Control on an already rooted Harmony Hub over SSH.",
    )
    parser.add_argument("--hub-host", "--host", dest="hub_host", default="", help="Harmony Hub IP address or hostname")
    parser.add_argument("--key-path", default="", help="SSH private key for root login; defaults to ~/.ssh/harmony_owner_*")
    parser.add_argument("--port", type=int, default=22, help="SSH port")
    parser.add_argument("--ssh-user", default="root", help="SSH username")
    parser.add_argument("--hub-id", default="", help="Hub ID from the root tool output")
    parser.add_argument("--mqtt-broker", default="", help="MQTT broker host/IP; leave blank to disable MQTT")
    parser.add_argument("--mqtt-port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--mqtt-user", default="", help="MQTT username")
    parser.add_argument("--mqtt-password", default="", help="MQTT password")
    parser.add_argument("--mqtt-base-topic", default="harmony/hub", help="MQTT base topic")
    parser.add_argument("--mqtt-discovery-prefix", default="homeassistant", help="Home Assistant MQTT discovery prefix")
    parser.add_argument("--mqtt-client-id", default="harmony-local-mqtt", help="MQTT client ID")
    parser.add_argument("--mqtt-disabled", action="store_true", help="Install with MQTT disabled")
    parser.add_argument("--skip-cloud-suppression", action="store_true", help="Do not replace netservicestarter.lua")
    parser.add_argument("--no-apply-cloud-restart", action="store_true", help="Do not reboot after enabling the Logitech cloud blocker")
    parser.add_argument("--no-prompt", action="store_true", help="Fail instead of asking for missing required values")
    parser.add_argument("--backup-dir", default="", help="Sensitive off-device backup directory")
    parser.add_argument("--backup-only", action="store_true", help="Verify a backup without replacing the working installation")
    parser.add_argument("--release-public-key", default="", help="Hexadecimal Ed25519 release public key; private signing key stays off the hub")
    parser.add_argument("--experimental-blank-bootstrap", action="store_true", help="Opt-in missing-resource bootstrap for disposable/blank-hub validation; never resets existing devices")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    try:
        args = parse_args(argv)
        args.hub_host = prompt_if_missing(args.hub_host, "Harmony hub IP address", True, args.no_prompt)

        if not args.key_path:
            default_key = resolve_default_key_path()
            if default_key:
                args.key_path = str(default_key)
                info(f"using SSH key {args.key_path}")
        args.key_path = prompt_if_missing(args.key_path, "SSH private key path for root login", True, args.no_prompt)
        key_path = Path(args.key_path).expanduser()
        if not key_path.is_file():
            raise RuntimeError(f"SSH key not found: {key_path}")
        args.key_path = str(key_path)

        if not args.no_prompt and args.mqtt_broker:
            if not args.mqtt_user:
                args.mqtt_user = input("MQTT username (blank if none): ").strip()
            if not args.mqtt_password:
                args.mqtt_password = getpass.getpass("MQTT password (blank if none): ")

        Installer(args).run()
        return 0
    except KeyboardInterrupt:
        fail("cancelled")
    except Exception as exc:
        fail(str(exc))
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
