"""Exercise boot and network recovery in temporary directories, never a hub."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix="harmony-recovery-") as directory:
        base = Path(directory)
        local = base / "local"
        local.mkdir()
        wifi, calls, fail = base / "wifi.conf", base / "calls", base / "fail"
        helper = base / "helper"
        helper.write_text(f'''#!/bin/sh
case "$1" in
 --update-health) [ ! -f '{fail}' ] ;;
 --rollback) echo rollback >> '{calls}'; rm -f '{local}/update.pending' ;;
esac
''')
        helper.chmod(0o755)
        script = (ROOT / "payload/scripts/release_recovery.sh").read_text()
        script = script.replace("/data/codex/local", str(local)).replace("/etc/wpa_supplicant.conf", str(wifi))
        script = script.replace("/cache/harmony-recovery", str(helper)).replace("sleep 1", "sleep 0")
        script = script.replace("/sbin/reboot", f"echo reboot >> '{calls}'")
        assert "/sbin/reboot" not in script and "/data/" not in script and "/cache/" not in script
        recovery = base / "recover.sh"
        recovery.write_text(script)
        wifi.write_text("bad network")
        (local / "wifi.previous").write_text("previous network")
        (local / "wifi.pending").write_text("1")
        subprocess.run(["sh", str(recovery), "recover-network"], check=True)
        assert wifi.read_text() == "previous network" and not (local / "wifi.pending").exists()
        assert os.stat(wifi).st_mode & 0o777 == 0o600
        (local / "update.pending").write_text("1")
        subprocess.run(["sh", str(recovery), "update-check"], check=True)
        assert not calls.exists() and not (local / "update.pending").exists()
        (local / "update.pending").write_text("1")
        fail.touch()
        subprocess.run(["sh", str(recovery), "update-check"], check=True)
        assert calls.read_text() == "rollback\nreboot\n" and not (local / "update.pending").exists()
        # Core readiness precedes the HBus listener on this hub. Discovery must wait for both.
        init = (ROOT / "payload/scripts/init.sh").read_text()
        block = init[init.index("  ready=0"):init.index(") &", init.index("  ready=0"))]
        core, tcp, discover = base / "core-ready", base / "tcp", base / "discover"
        block = block.replace("/tmp/harmony-operations/core-ready", str(core)).replace("/proc/net/tcp", str(tcp))
        block = block.replace("/data/codex/bin/codex_hbus", str(discover)).replace("sleep 1", "sleep 0")
        discover.write_text(f"#!/bin/sh\necho discovered >> '{calls}'\n")
        discover.chmod(0o755)
        startup = base / "startup.sh"
        startup.write_text(f"HUB_ID=19111956\nLOG='{base / 'startup.log'}'\n" + block)
        calls.unlink(); core.touch(); tcp.write_text("")
        subprocess.run(["sh", str(startup)], check=True)
        assert not calls.exists(), "core-ready alone must not start discovery"
        tcp.write_text("7F000001:1F98 00000000:0000 0A\n")
        core.unlink()
        subprocess.run(["sh", str(startup)], check=True)
        assert not calls.exists(), "the listener alone must not start discovery"
        core.touch()
        subprocess.run(["sh", str(startup)], check=True)
        assert calls.read_text() == "discovered\n"
    print("Network recovery, update health/rollback and bounded integration startup checks passed")


if __name__ == "__main__":
    run()
