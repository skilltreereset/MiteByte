"""Compile and run production lock/display code with fake hardware IO."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
compiler = shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("Install g++ or clang++ to run the native regression tests")
with tempfile.TemporaryDirectory(prefix="fleabyte-tests-") as folder:
    binary = pathlib.Path(folder) / "lock_test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra",
        "-I", str(ROOT / "tests/fakes"), "-I", str(ROOT / "fleabyte"),
        str(ROOT / "tests/lock_test.cpp"),
        str(ROOT / "fleabyte/lock.cpp"),
        str(ROOT / "fleabyte/lock_validation.cpp"),
        str(ROOT / "fleabyte/ui_display.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
