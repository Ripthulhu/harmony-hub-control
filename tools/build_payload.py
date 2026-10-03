"""Build native payloads with an explicitly supplied MIPS/uClibc compiler."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / "payload/source"
args.output.mkdir(parents=True, exist_ok=True)
for name in ("codex_webui", "codex_hbus", "codex_hal_ltcp", "codex_bthid_keyboard", "codex_portal"):
    files = [source / (name + ".c")]
    if name == "codex_webui":
        files += [source / "vendor" / p for p in ("cJSON.c", "monocypher.c", "monocypher-ed25519.c")]
    subprocess.run([args.cc, "-Os", "-static", "-s", "-DCJSON_NESTING_LIMIT=32"] +
                   [str(p) for p in files] + ["-o", str(args.output / name)], check=True)
    print(f"{name}: {(args.output / name).stat().st_size} bytes")
