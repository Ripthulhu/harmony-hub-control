"""Restore a verified desktop backup through existing owner SSH."""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile
from install_webui import Installer, PAYLOAD, remote_quote


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backup", type=Path)
    parser.add_argument("--hub-host", required=True)
    parser.add_argument("--key-path", required=True)
    parser.add_argument("--port", type=int, default=22)
    parser.add_argument("--ssh-user", default="root")
    parser.add_argument("--apply", action="store_true", help="Without this flag only validate the backup")
    parser.add_argument("--reboot", action="store_true")
    args = parser.parse_args()
    with zipfile.ZipFile(args.backup) as archive:
        manifest = json.loads(archive.read("backup-manifest.json"))
        if manifest.get("schemaVersion") != 1 or manifest.get("host") != args.hub_host:
            raise ValueError("Backup schema or hub identity does not match")
        if len(manifest["files"]) > 512:
            raise ValueError("Backup file limit exceeded")
        files, seen = [], {}
        for record in manifest["files"]:
            path = record["path"]
            if not path.startswith(("/data/", "/pkg/", "/etc/", "/opt/", "/usr/", "/home/")) or ".." in path.split("/"):
                raise ValueError("Invalid or duplicate backup path")
            if path in seen:
                if seen[path] != record:
                    raise ValueError("Conflicting duplicate backup file")
                continue
            seen[path] = record
            raw = archive.read(path.lstrip("/"))
            if len(raw) != record["size"] or len(raw) > 2 * 1024 * 1024 or hashlib.sha256(raw).hexdigest() != record["sha256"]:
                raise ValueError("Backup file hash or length mismatch")
            files.append((path, raw, format(record["mode"] & 0o777, "o")))
    print(f"Verified {len(files)} files for {args.hub_host}")
    if not args.apply:
        print("No hub changes made. Use --apply to restore through owner SSH.")
        return
    installer = Installer(args)
    installer.run_remote("id", quiet=True)
    installer.upload_data((PAYLOAD / "bin" / "codex_webui").read_bytes(), "/tmp/harmony-restore-preflight", "755")
    installer.preflight = "/tmp/harmony-restore-preflight"
    for path, raw, mode in files:
        installer.upload_data(raw, path, mode)
        result = installer.run_remote("md5sum " + remote_quote(path), quiet=True).split()[0]
        if result != hashlib.md5(raw).hexdigest():
            raise RuntimeError(f"Restore verification failed: {path}")
    installer.run_remote(installer.preflight + " --sync", quiet=True)
    if args.reboot:
        installer.run_remote("(sleep 2; /sbin/reboot) >/dev/null 2>&1 & echo restarting", quiet=True)
    print("Previous userspace files restored; reboot is needed to activate them.")


if __name__ == "__main__":
    main()
