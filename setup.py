"""One desktop setup flow using the existing Harmony Hub Tool for USB and LAN root."""
import argparse
from pathlib import Path
import socket
import subprocess
import sys
import time
import webbrowser
from install_webui import Installer, parse_args, resolve_default_key_path


def discover():
    request = b'M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n'
    found = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(.25)
        sock.sendto(request, ("239.255.255.250", 1900))
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                data, source = sock.recvfrom(8192)
                if b"harmony" in data.lower():
                    found.add(source[0])
            except socket.timeout:
                pass
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hub-host", default="")
    parser.add_argument("--key-path", default="")
    parser.add_argument("--root-tool", type=Path, default=Path(__file__).resolve().parent.parent / "harmony-hub-root" / "run_harmony_hub_tool.py")
    args = parser.parse_args()
    if not args.hub_host:
        if input("Does the hub need Wi-Fi provisioned over USB? [y/N]: ").lower() == "y":
            if not args.root_tool.is_file():
                raise RuntimeError("Supply --root-tool pointing to the existing harmony-hub-root checkout")
            subprocess.run([sys.executable, str(args.root_tool), "--action", "usb-provision-wifi", "--save-hub-id"], check=True)
        hubs = discover()
        if hubs:
            print("Discovered hubs: " + ", ".join(hubs))
        args.hub_host = input("Hub IP address" + (f" [{hubs[0]}]" if len(hubs) == 1 else "") + ": ").strip()
        if not args.hub_host and len(hubs) == 1:
            args.hub_host = hubs[0]
    socket.inet_aton(args.hub_host)
    key = Path(args.key_path).expanduser() if args.key_path else resolve_default_key_path()
    if not key:
        key = Path.home() / ".ssh" / "harmony_owner_ed25519"
    install_args = parse_args(["--hub-host", args.hub_host, "--key-path", str(key), "--no-prompt"])
    installer = Installer(install_args)
    try:
        installer.run_remote("id", timeout=10, quiet=True)
    except (RuntimeError, subprocess.TimeoutExpired):
        if not args.root_tool.is_file():
            raise RuntimeError("Owner SSH is unavailable. Supply --root-tool pointing to the existing LAN root tool")
        subprocess.run([sys.executable, str(args.root_tool), "--action", "lan-root", "--hub-host", args.hub_host,
                        "--private-key", str(key), "--pubkey", str(key) + ".pub", "--no-shell"], check=True)
    installer.run()
    webbrowser.open(f"http://{args.hub_host}:8080/")
    print("Open the local UI and enter the single-use code printed above. No Logitech login is used by this installer.")


if __name__ == "__main__":
    main()
