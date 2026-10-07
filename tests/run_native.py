"""Compile and run production lock/display code with fake hardware IO."""
import pathlib
import shutil
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
import sys
subprocess.run([sys.executable, str(ROOT / "tests/check_partitions.py")], check=True)
compiler = shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("Install g++ or clang++ to run the native regression tests")
with tempfile.TemporaryDirectory(prefix="mitebyte-tests-") as folder:
    binary = pathlib.Path(folder) / "lock_test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra",
        "-I", str(ROOT / "tests/fakes"), "-I", str(ROOT / "mitebyte"),
        str(ROOT / "tests/lock_test.cpp"),
        str(ROOT / "mitebyte/lock.cpp"),
        str(ROOT / "mitebyte/lock_validation.cpp"),
        str(ROOT / "mitebyte/ui_display.cpp"),
        str(ROOT / "mitebyte/menu_view.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "menu_view_test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra",
        "-I", str(ROOT / "tests/fakes"), "-I", str(ROOT / "mitebyte"),
        str(ROOT / "tests/menu_view_test.cpp"),
        str(ROOT / "mitebyte/menu_view.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "usb_drive_test.exe"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-pthread",
        "-I", str(ROOT / "tests/sd_native"),
        str(ROOT / "tests/usb_drive_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "usb_rx_buffers_test.exe"
    tx_binary = pathlib.Path(folder) / "usb_tx_scratch_test.exe"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-pthread",
        "-I", str(ROOT / "tests/diagnostic_native"),
        str(ROOT / "tests/usb_tx_scratch_test.cpp"), "-o", str(tx_binary)], check=True)
    subprocess.run([str(tx_binary)], check=True)
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-pthread",
        str(ROOT / "tests/usb_rx_buffers_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "hotspot_diagnostics_test.exe"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-pthread", "-DHOTSPOT_DIAGNOSTICS=1",
        "-I", str(ROOT / "tests/diagnostic_native"), "-I", str(ROOT / "tests/fakes"),
        str(ROOT / "tests/hotspot_diagnostics_test.cpp"), str(ROOT / "mitebyte/src/diagnostics/diag_log.cpp"),
        "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "diagnostic_packet_test.exe"
    subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", str(ROOT / "tests/diagnostic_packet_test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "tools_test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra",
        "-I", str(ROOT / "tests/fakes"), "-I", str(ROOT / "mitebyte"),
        str(ROOT / "tests/tools_test.cpp"), str(ROOT / "mitebyte/tools.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    binary = pathlib.Path(folder) / "hotspot_dns_test.exe"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra",
        "-I", str(ROOT / "tests/fakes"), "-I", str(ROOT / "mitebyte"),
        str(ROOT / "tests/hotspot_dns_test.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
