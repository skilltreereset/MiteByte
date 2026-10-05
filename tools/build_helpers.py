"""Bundle the reviewed Windows setup with the firmware, without dependencies."""
from pathlib import Path
import base64
import gzip
import hashlib
import io
if "__file__" in globals():
    root = Path(__file__).resolve().parents[1]
else:  # PlatformIO evaluates extra scripts through SCons.
    Import("env")
    root = Path(env.subst("$PROJECT_DIR"))
output = root / ".cache/generated"
output.mkdir(parents=True, exist_ok=True)
script = (root / "tools/windows/FleaByte-Sharing.ps1").read_text(encoding="utf-8")
launcher = '''@echo off
set "FLEABYTE_SETUP_FILE=%~f0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:FLEABYTE_SETUP_FILE); $p=$s.Substring($s.IndexOf('# FLEABYTE_'+'POWERSHELL')); & ([scriptblock]::Create($p)) -Install -SourcePath $env:FLEABYTE_SETUP_FILE"
set "FLEABYTE_SETUP_EXIT=%errorlevel%"
if /I "%~1"=="/no-pause" exit /b %FLEABYTE_SETUP_EXIT%
pause
exit /b %FLEABYTE_SETUP_EXIT%
# FLEABYTE_POWERSHELL
'''
package = launcher + script
(output / "FleaByte-Sharing-Setup.cmd").write_bytes(package.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
header = '#pragma once\nstatic const char WINDOWS_SHARING_SETUP[] PROGMEM = R"FLEASETUP(' + package + ')FLEASETUP";\n'
target = root / "fleabyte/generated/windows_setup.h"
target.parent.mkdir(parents=True, exist_ok=True)
if not target.exists() or target.read_text(encoding="utf-8") != header:
    target.write_text(header, encoding="utf-8")

# The keyboard setup uses the exact same reviewed package. Compression keeps
# it within the interpreter's 16 KiB limit; no network download is needed.
package_bytes = package.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8")
compressed = io.BytesIO()
with gzip.GzipFile(fileobj=compressed, mode="wb", filename="", mtime=0) as stream:
    stream.write(package_bytes)
encoded = base64.b64encode(compressed.getvalue()).decode("ascii")
checksum = hashlib.sha256(package_bytes).hexdigest().upper()
bootstrap = (
    "$ErrorActionPreference='Stop'; "
    "Write-Host 'FleaByte Wi-Fi Hotspot setup: unpacking the installer. Approve the Windows administrator prompt to install.'; "
    f"$b=[Convert]::FromBase64String('{encoded}'); "
    "$m=[IO.MemoryStream]::new([byte[]]$b,$false); "
    "$z=[IO.Compression.GZipStream]::new($m,[IO.Compression.CompressionMode]::Decompress); "
    "$o=[IO.MemoryStream]::new(); $z.CopyTo($o); $data=$o.ToArray(); $z.Dispose(); $m.Dispose(); $o.Dispose(); "
    "$hash=[Security.Cryptography.SHA256]::Create(); "
    "$actual=[BitConverter]::ToString($hash.ComputeHash($data)).Replace('-',''); $hash.Dispose(); "
    f"if($actual -ne '{checksum}'){{throw 'The setup payload is incomplete. Nothing was installed.'}}; "
    "$p=Join-Path ([IO.Path]::GetTempPath()) ('FleaByte-Sharing-'+[guid]::NewGuid().ToString('N')+'.cmd'); "
    "[IO.File]::WriteAllBytes($p,$data); "
    "try { & $p /no-pause; if($LASTEXITCODE -ne 0){throw 'Installation did not finish.'} } finally { Remove-Item -LiteralPath $p -Force }; "
    "Write-Host 'Return to the FleaByte Library and start Wi-Fi Hotspot after installation succeeds.'"
)
payload = (
    "META windows\n"
    "REM One-time Wi-Fi Hotspot setup on a PC you manage.\n"
    "REM Stop Wi-Fi Hotspot first; setup needs the USB keyboard.\n"
    "REM Select the PC keyboard layout before running. Do not type while it runs.\n"
    "REM Opens a visible PowerShell and the same installer as Download setup.\n"
    "REM You must approve Windows elevation yourself, with an admin password if asked.\n"
    "REM Wait for installation to succeed, then start Wi-Fi Hotspot in the Library.\n"
    "WAIT_FOR_HOST 5000\n"
    "DEFAULTCHARDELAY 2\n"
    "GUI r\n"
    "DELAY 800\n"
    "STRINGLN powershell.exe -NoLogo -NoProfile\n"
    "DELAY 5000\n"
    "STRINGLN " + bootstrap + "\n"
)
if len(payload.encode("ascii")) > 16384 or len(bootstrap) >= 8000:
    raise ValueError("Windows setup exceeds keyboard payload/console limits")
payload_header = '#pragma once\nstatic const char WINDOWS_SHARING_PAYLOAD[] PROGMEM = R"FLEAPAYLOAD(' + payload + ')FLEAPAYLOAD";\n'
target = root / "fleabyte/generated/windows_setup_payload.h"
if not target.exists() or target.read_text(encoding="utf-8") != payload_header:
    target.write_text(payload_header, encoding="utf-8")
(output / "13-windows-hotspot-setup.txt").write_text(payload, encoding="utf-8")

# Default payloads live as editable files in fleabyte/payloads/ rather than as C
# constants. Embed them into a generated table that storage.cpp seeds into
# LittleFS on first boot / seed-version bump. The hotspot payload (13-...) stays
# generated above from the reviewed Windows installer and is seeded separately.
payload_dir = root / "fleabyte/payloads"
entries = sorted(payload_dir.glob("*.txt"), key=lambda p: p.name)
lines = [
    "#pragma once",
    "// Generated from fleabyte/payloads/*.txt by tools/build_helpers.py. Do not edit.",
    "struct SeededPayload { const char *name; const char *body; };",
]
table = []
for i, p in enumerate(entries):
    body = p.read_text(encoding="utf-8").replace("\r\n", "\n")
    if ")FLEAPL\"" in body:
        raise ValueError(f"{p.name} contains the raw-string delimiter")
    lines.append(f'static const char FLEA_PAYLOAD_{i}[] PROGMEM = R"FLEAPL({body})FLEAPL";')
    table.append(f'  {{"{p.name}", FLEA_PAYLOAD_{i}}},')
lines.append("static const SeededPayload SEEDED_PAYLOADS[] = {")
lines.extend(table)
lines.append("};")
payloads_header = "\n".join(lines) + "\n"
target = root / "fleabyte/generated/payloads.h"
if not target.exists() or target.read_text(encoding="utf-8") != payloads_header:
    target.write_text(payloads_header, encoding="utf-8")
