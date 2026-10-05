"""Crash storage must not relocate the settings filesystem or firmware starts."""
from pathlib import Path
import csv

root = Path(__file__).resolve().parents[1]
for filename, app1, filesystem, fs_size in (
    ("partitions.csv", 0x610000, 0xC10000, 0x3E8000),
    ("fleabyte/partitions.csv", 0x410000, 0x810000, 0x7E0000),
):
    entries = {}
    for row in csv.reader((root / filename).read_text().splitlines()):
        if not row or row[0].lstrip().startswith("#"):
            continue
        name, kind, subtype, offset, size = [field.strip() for field in row[:5]]
        entries[name] = (int(offset, 0), int(size, 0))
    assert entries["nvs"] == (0x9000, 0x5000)
    assert entries["otadata"] == (0xE000, 0x2000)
    assert entries["app0"][0] == 0x10000
    assert entries["app1"][0] == app1
    assert entries["spiffs"] == (filesystem, fs_size)
    assert entries["coredump"] == (filesystem - 0x40000, 0x40000)
    end = 0x9000
    for offset, size in sorted(entries.values()):
        assert offset >= end and offset % 0x1000 == 0 and size % 0x1000 == 0
        end = offset + size
    assert end <= 16 * 1024 * 1024
print("PASS: crash partition space, nonoverlap, unchanged filesystem and firmware starts")
