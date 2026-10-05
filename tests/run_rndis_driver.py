"""Exercise the production C driver against the installed TinyUSB headers."""
from pathlib import Path
import shutil
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
sdk = root / '.pio-core/packages/framework-arduinoespressif32-libs/esp32s3/include/arduino_tinyusb/tinyusb/src'
if not sdk.is_dir():
    raise SystemExit('Build firmware with PlatformIO first to install the TinyUSB headers.')
compiler = shutil.which('gcc') or shutil.which('clang')
if not compiler:
    raise SystemExit('A native C compiler is required.')
with tempfile.TemporaryDirectory(prefix='fleabyte-rndis-') as folder:
    binary = Path(folder) / 'driver.exe'
    subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', '-I', str(root / 'tests/rndis_native'),
                    '-I', str(sdk), str(root / 'tests/rndis_driver_test.c'),
                    str(root / 'fleabyte/src/usb_rndis/ecm_rndis_device.c'),
                    str(root / 'fleabyte/src/usb_rndis/rndis_reports.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    cxx = shutil.which('g++') or shutil.which('clang++')
    if not cxx:
        raise SystemExit('A native C++ compiler is required for USB lifecycle checks.')
    binary = Path(folder) / 'usb-mode.exe'
    subprocess.run([cxx, '-std=c++17', '-Wall', '-Wextra',
                    '-I', str(root / 'tests/usb_mode_native'), '-I', str(root / 'tests/fakes'),
                    '-I', str(root / 'tests/rndis_native'), '-I', str(sdk),
                    str(root / 'tests/usb_mode_test.cpp'), str(root / 'fleabyte/usb_mode.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
