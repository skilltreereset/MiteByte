"""Ensure the downloadable installer exactly embeds the reviewed helper."""
from pathlib import Path
import runpy
import base64
import gzip
import hashlib
import re

root = Path(__file__).resolve().parents[1]
header_path = root / "fleabyte/generated/windows_setup.h"
before = header_path.read_bytes()
payload_path = root / "fleabyte/generated/windows_setup_payload.h"
payload_before = payload_path.read_bytes()
runpy.run_path(str(root / "tools/build_helpers.py"))
assert header_path.read_bytes() == before, "Regenerate windows_setup.h after changing the helper"
assert payload_path.read_bytes() == payload_before, "Regenerate windows_setup_payload.h after changing the helper"
header = header_path.read_text(encoding="utf-8")
package = header.split('R"FLEASETUP(', 1)[1].rsplit(')FLEASETUP"', 1)[0]
download = (root / ".cache/generated/FleaByte-Sharing-Setup.cmd").read_text(encoding="utf-8")
assert package == download
marker = "# FLEABYTE_POWERSHELL\n"
launcher, helper = package.split(marker, 1)
assert helper == (root / "tools/windows/FleaByte-Sharing.ps1").read_text(encoding="utf-8")
assert launcher.startswith("@echo off\n")
assert "-Install -SourcePath $env:FLEABYTE_SETUP_FILE" in launcher
assert "set \"FLEABYTE_SETUP_EXIT=%errorlevel%\"" in launcher
assert 'if /I "%~1"=="/no-pause" exit /b' in launcher
assert ')FLEASETUP"' not in package
print("PASS: firmware/download setup content, source helper, launcher and exit status")

payload = payload_path.read_text(encoding="utf-8").split('R"FLEAPAYLOAD(', 1)[1].rsplit(')FLEAPAYLOAD"', 1)[0]
assert payload == (root / ".cache/generated/13-windows-hotspot-setup.txt").read_text(encoding="utf-8")
assert len(payload.encode("ascii")) <= 16384
commands = [line for line in payload.splitlines() if line and not line.startswith(("REM ", "META "))]
assert commands[:6] == ["WAIT_FOR_HOST 5000", "DEFAULTCHARDELAY 2", "GUI r", "DELAY 800",
                        "STRINGLN powershell.exe -NoLogo -NoProfile", "DELAY 5000"]
assert len(commands) == 7, "No automated elevation-response keystrokes"
bootstrap = commands[-1].removeprefix("STRINGLN ")
assert len(bootstrap) < 8000
encoded = re.search(r"FromBase64String\('([^']+)'\)", bootstrap).group(1)
unpacked = gzip.decompress(base64.b64decode(encoded))
assert unpacked == (root / ".cache/generated/FleaByte-Sharing-Setup.cmd").read_bytes()
assert hashlib.sha256(unpacked).hexdigest().upper() in bootstrap
assert "& $p /no-pause" in bootstrap and "$LASTEXITCODE -ne 0" in bootstrap
assert "finally { Remove-Item -LiteralPath $p -Force }" in bootstrap
assert "http" not in bootstrap.lower(), "The setup payload must work without downloading code"
print("PASS: keyboard setup exact installer, integrity check, limits and explicit Windows approval")
