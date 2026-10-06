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
script = (root / "tools/windows/MiteByte-Sharing.ps1").read_text(encoding="utf-8")
launcher = '''@echo off
set "MITEBYTE_SETUP_FILE=%~f0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "$s=[IO.File]::ReadAllText($env:MITEBYTE_SETUP_FILE); $p=$s.Substring($s.IndexOf('# MITEBYTE_'+'POWERSHELL')); & ([scriptblock]::Create($p)) -Install -SourcePath $env:MITEBYTE_SETUP_FILE"
set "MITEBYTE_SETUP_EXIT=%errorlevel%"
if /I "%~1"=="/no-pause" exit /b %MITEBYTE_SETUP_EXIT%
pause
exit /b %MITEBYTE_SETUP_EXIT%
# MITEBYTE_POWERSHELL
'''
package = launcher + script
(output / "MiteByte-Sharing-Setup.cmd").write_bytes(package.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
header = '#pragma once\nstatic const char WINDOWS_SHARING_SETUP[] PROGMEM = R"FLEASETUP(' + package + ')FLEASETUP";\n'
target = root / "mitebyte/generated/windows_setup.h"
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
    "Write-Host 'MiteByte Wi-Fi Hotspot setup: unpacking the installer. Approve the Windows administrator prompt to install.'; "
    f"$b=[Convert]::FromBase64String('{encoded}'); "
    "$m=[IO.MemoryStream]::new([byte[]]$b,$false); "
    "$z=[IO.Compression.GZipStream]::new($m,[IO.Compression.CompressionMode]::Decompress); "
    "$o=[IO.MemoryStream]::new(); $z.CopyTo($o); $data=$o.ToArray(); $z.Dispose(); $m.Dispose(); $o.Dispose(); "
    "$hash=[Security.Cryptography.SHA256]::Create(); "
    "$actual=[BitConverter]::ToString($hash.ComputeHash($data)).Replace('-',''); $hash.Dispose(); "
    f"if($actual -ne '{checksum}'){{throw 'The setup script is incomplete. Nothing was installed.'}}; "
    "$p=Join-Path ([IO.Path]::GetTempPath()) ('MiteByte-Sharing-'+[guid]::NewGuid().ToString('N')+'.cmd'); "
    "[IO.File]::WriteAllBytes($p,$data); "
    "try { & $p /no-pause; if($LASTEXITCODE -ne 0){throw 'Installation did not finish.'} } finally { Remove-Item -LiteralPath $p -Force }; "
    "Write-Host 'Return to the MiteByte Library and start Wi-Fi Hotspot after installation succeeds.'"
)
script = (
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
    "DELAY 500\n"
    "ALT y\n"
)
if len(script.encode("ascii")) > 16384 or len(bootstrap) >= 8000:
    raise ValueError("Windows setup exceeds keyboard script/console limits")
script_header = '#pragma once\nstatic const char WINDOWS_SHARING_SCRIPT[] PROGMEM = R"FLEASCRIPT(' + script + ')FLEASCRIPT";\n'
target = root / "mitebyte/generated/windows_setup_script.h"
if not target.exists() or target.read_text(encoding="utf-8") != script_header:
    target.write_text(script_header, encoding="utf-8")
(output / "13-windows-hotspot-setup.txt").write_text(script, encoding="utf-8")

# Default scripts live as editable files in mitebyte/src/scripts/ rather than as C
# constants. Embed them into a generated table that storage.cpp seeds into
# LittleFS on first boot / seed-version bump. The hotspot script (13-...) stays
# generated above from the reviewed Windows installer and is seeded separately.
script_dir = root / "mitebyte/src/scripts"
entries = sorted(script_dir.glob("*.txt"), key=lambda p: p.name)
lines = [
    "#pragma once",
    "// Generated from mitebyte/src/scripts/*.txt by tools/build_helpers.py. Do not edit.",
    "struct SeededScript { const char *name; const char *body; };",
]
table = []
for i, p in enumerate(entries):
    body = p.read_text(encoding="utf-8").replace("\r\n", "\n")
    if ")FLEAPL\"" in body:
        raise ValueError(f"{p.name} contains the raw-string delimiter")
    lines.append(f'static const char FLEA_SCRIPT_{i}[] PROGMEM = R"FLEAPL({body})FLEAPL";')
    table.append(f'  {{"{p.name}", FLEA_SCRIPT_{i}}},')
lines.append("static const SeededScript SEEDED_SCRIPTS[] = {")
lines.extend(table)
lines.append("};")
scripts_header = "\n".join(lines) + "\n"
target = root / "mitebyte/generated/scripts.h"
if not target.exists() or target.read_text(encoding="utf-8") != scripts_header:
    target.write_text(scripts_header, encoding="utf-8")
