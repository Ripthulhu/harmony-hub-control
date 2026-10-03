"""Build a signed userspace release. The signing key must stay off the hub."""
import argparse
import base64
import hashlib
import json
from pathlib import Path
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

ROOT = Path(__file__).resolve().parents[1]
FILES = {name: ROOT / "payload" / "bin" / name for name in
         ("codex_webui", "codex_hbus", "codex_hal_ltcp", "codex_bthid_keyboard", "codex_portal")}
FILES.update({name: ROOT / "payload" / "www" / name for name in
              ("index.html", "app.css", "app.js", "icons.svg", "profiles.js")})
FILES.update({"localcore.lua": ROOT / "payload/core/localcore.lua",
              "codexmqtt.lua": ROOT / "payload/mqtt/codexmqtt.lua",
              "init.sh": ROOT / "payload/scripts/init.sh",
              "maintenance.sh": ROOT / "payload/scripts/maintenance.sh",
              "migrations.json": ROOT / "payload/core/migrations.json"})


def bundle(key, version, files):
    data = {name: path.read_bytes() for name, path in files.items()}
    manifest = json.dumps({"schemaVersion": 1, "version": version, "platform": "pimento-4.15.600",
        "files": [{"name": name, "size": len(raw), "sha512": hashlib.sha512(raw).hexdigest()}
                  for name, raw in data.items()]}, separators=(",", ":"))
    return {"manifest": manifest, "signature": key.sign(manifest.encode()).hex(),
            "files": {name: base64.b64encode(raw).decode() for name, raw in data.items()}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    key = serialization.load_pem_private_key(args.key.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError("An Ed25519 signing key is required")
    args.out.write_text(json.dumps(bundle(key, args.version, FILES), separators=(",", ":")), encoding="utf-8")
    print(f"Signed release: {args.out}")
