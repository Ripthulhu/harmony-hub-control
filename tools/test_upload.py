"""Run with python tools/test_upload.py; does not contact a hub."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from install_webui import Installer, remote_quote

installer = object.__new__(Installer)
calls = []
installer.run_remote = lambda *args, **kwargs: calls.append((args, kwargs))
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "bytes.bin"
    path.write_bytes(b"\0\xff\nbinary")
    with contextlib.redirect_stdout(io.StringIO()) as output:
        installer.upload_bytes(path, "/tmp/it's here", "755")
        installer.upload_text("caf\u00e9\n", "/data/config.json", "600")
        installer.upload_text("", "/tmp/empty", "600")
    assert calls[0][0][1] == path.read_bytes()
    assert calls[1][0][1] == "caf\u00e9\n".encode()
    assert calls[2][0][1] == b""
    assert remote_quote("/tmp/it's here") in calls[0][0][0]
    assert "&& mv " in calls[0][0][0] and "&& chmod 755 " in calls[0][0][0]
    assert all(kwargs["timeout"] >= 90 and kwargs["quiet"] for _, kwargs in calls)
    assert "/data/config.json bytes=6 md5=<hidden>" in output.getvalue()
print("Installer binary/text upload, quoting, and secret-output checks passed")
