# Build Notes

The repository ships ready-to-install MIPS binaries in `payload/bin/`. Rebuild
only when native source changes.

For the standalone service, use the pinned sources in `payload/source/vendor`
and an existing MIPS/uClibc compiler:

```sh
python3 tools/build_payload.py --cc /path/to/mips-buildroot-linux-uclibc-gcc --output build/output
```

The UI source is in `payload/www`. Its Lucide sprite and profile parsers are
packaged with `tools/package_assets.mjs`; there are no runtime CDN dependencies.

## Target

- CPU: MIPS, big-endian
- Userspace: uClibc-era embedded Linux
- Output: mostly static helper binaries

## Linux Build

The helper build script is written for a Debian/Kali-like Linux environment:

```sh
cd build
./build_harmony_tools_kali.sh
```

It downloads the Bootlin MIPS uClibc toolchain and Dropbear source into
`build/toolchains/` and `build/tmp/`, then writes fresh binaries to
`build/output/`.

After rebuilding:

1. Copy the required binaries from `build/output/` into `payload/bin/`.
2. Refresh `payload/bin/MANIFEST.txt`.
3. Deploy to a test hub with `install_webui.ps1` or `python3 install_webui.py`.
4. Confirm checksums and runtime behavior.

## Windows

Windows can deploy with PowerShell, and Linux/macOS can deploy with the Python
installer. Use WSL, a Linux VM, or the existing build server for rebuilding MIPS
binaries.

## Dropbear

`dropbearmulti` is included so the web UI package can keep SSH reachable on a
rooted hub. The installer does not replace `authorized_keys`; it only uploads
Dropbear binaries/wrappers and starts the service if needed.

## Runtime Checks

From the repository root on Linux:

```sh
gcc -O0 tools/test_ir_runtime.c payload/source/vendor/cJSON.c payload/source/vendor/monocypher.c payload/source/vendor/monocypher-ed25519.c -o /tmp/test-ir && /tmp/test-ir
gcc -O0 tools/test_hbus_hold.c -o /tmp/test-hold && /tmp/test-hold
lua5.1 tools/test_mqtt_runtime.lua
node tools/test_ir_controls.mjs
node tools/test_local_controls.mjs
python3 tools/test_local_api.py
python3 tools/test_upload.py
```

These check IR responses, holds, release timeouts, LTCP framing, MQTT discovery
and reloads, browser button handling, and uploads without contacting a device.
Run the C checks on the build host, not a hub.

On Windows, also run `powershell -NoProfile -File tools/test_ssh_helpers.ps1`.
The installer and rollback script share `ssh_helpers.ps1`; keep it alongside them.

## Signed Releases

Create and protect an Ed25519 PEM signing key off-device. Export its raw
32-byte public key as 64 hexadecimal characters in a text file. Install that
public key with `install_webui.py --release-public-key <public-key.txt>` through
owner SSH. No default development key is trusted.

With the desktop `cryptography` package installed:

```sh
python3 tools/release_bundle.py --key <private-key.pem> --version <version> --out <release.json>
```

The bundle covers the five native helpers, Lua core/MQTT, local assets, startup
scripts and schema migration metadata. Initial installation and changes to
stock listener restrictions use owner SSH, not browser updates. Schema 1
currently supports identity migrations only; reject later schemas until a
tested migration is supplied.

The boot recovery script and known-good helper are outside the replaceable
bundle. They validate startup and restore one previous release on failure.
Host tests exercise signature rejection, activation and exact rollback in
temporary directories. They do not reboot a real hub or replace the separate
blank-hub and offline acceptance gates.
