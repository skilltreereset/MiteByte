"""Check compiled Xtensa task/callback frames for the captured stack regression."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("elf", type=Path)
parser.add_argument("--objdump", type=Path)
args = parser.parse_args()
objdump = args.objdump or shutil.which("xtensa-esp32s3-elf-objdump")
if not objdump:
    suffix = ".exe" if shutil.which("powershell.exe") else ""
    objdump = root / ".pio-core/packages/toolchain-xtensa-esp-elf/bin" / ("xtensa-esp32s3-elf-objdump" + suffix)
output = subprocess.check_output([str(objdump), "-d", str(args.elf)], text=True)
entries = dict(re.findall(
    r"^[0-9a-f]+ <([^>]+)>:\n\s*[0-9a-f]+:\s+[0-9a-f]+\s+entry\s+a1,\s+(0x[0-9a-f]+|[0-9]+)",
    output, re.MULTILINE,
))
for name in ("_ZL8transmitPvS_j", "_ZL13receiveWorkerPv"):
    assert name in entries, f"Missing compiled callback: {name}"
    size = int(entries[name], 0)
    assert size <= 512, f"{name}: {size}-byte stack frame exceeds 512-byte budget"
    print(f"PASS: {name} compiled stack frame {size} bytes")
