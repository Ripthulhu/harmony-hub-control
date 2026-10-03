"""Normalize the Harmony Lua 5.1 numeric-header flag for read-only analysis."""
import pathlib
import subprocess
import sys

source, jar, output = map(pathlib.Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
for file in source.glob("*.lua"):
    data = file.read_bytes()
    if data[:12] != bytes.fromhex("1b4c75615100010404040804"):
        raise ValueError(f"Unexpected bytecode header: {file}")
    normalized = output / (file.name + ".normalized")
    normalized.write_bytes(data[:11] + b"\x00" + data[12:])
    with (output / file.name).open("w") as result:
        subprocess.run(["java", "-jar", str(jar), str(normalized)], stdout=result, check=True)
