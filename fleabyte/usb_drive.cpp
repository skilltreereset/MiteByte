#include "usb_drive.h"
#include "config.h"

#include "USB.h"
#include "USBMSC.h"
#include "SD_MMC.h"
#include <esp_system.h>
#include <esp_app_desc.h>
#include <esp_core_dump.h>
#include <esp_partition.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <algorithm>

extern "C" {
#include "ff.h"
#include "diskio.h"
}

// Global: its constructor registers the USB interface, which has to happen
// before the core opens USB in app_main().
static USBMSC MSC;

// Guards the card controller and the flags below. The watch task, the web
// handlers and the main loop all reach this state, and two tasks mounting
// at once leaves the card unidentifiable until a power cycle.
//
// MSC callbacks use the read-ahead gate for their whole sector operation.
// Revoking media alone does not cancel a callback already executing: remount
// must wait for that callback as well as the background read-ahead.
static SemaphoreHandle_t s_lock = nullptr;

static String s_deviceName = DEVICE_NAME_DEFAULT;
static bool s_cardPresent = false;
static bool s_exposed = false;
static bool s_requestedExposure = false;
static bool s_bootRecorded = false;
static uint64_t s_sizeMB = 0;

static void lockSd() {
  if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlockSd() {
  if (s_lock) xSemaphoreGive(s_lock);
}

static bool mountCardLocked();
static bool remountLocked();
static bool fsAvailableLocked();
static void cardWatchTask(void *arg);
static bool appendDiagnosticLocked(const char *data, size_t size, bool newSession);
static void recordBootLocked();

// Read-ahead. The USB stack asks onRead() for one buffer at a time and only
// asks for the next once this one has gone out to the host, so the card read
// and the USB transfer used to take turns. After serving a request, onRead()
// hands the following sectors to raTask, which reads them from the card while
// the current buffer is still on the wire. A sequential copy then finds its
// next buffer already in RAM.
//
// One card access at a time: every direct card access below (a read miss, a
// write, the remount in usbDriveSetExposed) waits for an in-flight read-ahead
// to finish first.
static const uint32_t RA_BYTES = 4096;  // TinyUSB's MSC buffer in the prebuilt core
DMA_ATTR static uint8_t s_raBuf[RA_BYTES];
static TaskHandle_t s_raTask = nullptr;
// Taken while a read-ahead is in flight, given back by raTask when it ends.
static SemaphoreHandle_t s_raIdle = nullptr;
// Orders enable/disable against new starts, so none begins mid-remount.
static SemaphoreHandle_t s_raGate = nullptr;
static bool s_raEnabled = false;
static volatile bool s_raValid = false;
static uint32_t s_raLba = 0;
static uint32_t s_raCount = 0;
static uint32_t s_raTotal = 0;
static uint32_t s_raHits = 0;
static uint32_t s_raMisses = 0;

class HostIoGuard {
public:
  HostIoGuard() { xSemaphoreTake(s_raGate, portMAX_DELAY); }
  ~HostIoGuard() { xSemaphoreGive(s_raGate); }
  bool ready() const { return s_raEnabled; }
};

static void raWaitIdle() {
  xSemaphoreTake(s_raIdle, portMAX_DELAY);
  xSemaphoreGive(s_raIdle);
}

static void raTask(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_raValid = disk_read(0, s_raBuf, s_raLba, s_raCount) == RES_OK;
    xSemaphoreGive(s_raIdle);
  }
}

// Caller holds s_raGate throughout the host sector operation.
static void raStartLocked(uint32_t lba, uint32_t secSize) {
  if (lba >= s_raTotal) return;
  uint32_t count = RA_BYTES / secSize;
  if (count > s_raTotal - lba) count = s_raTotal - lba;
  if (!count) return;

  if (s_raEnabled) {
    xSemaphoreTake(s_raIdle, portMAX_DELAY);
    s_raValid = false;
    s_raLba = lba;
    s_raCount = count;
    xTaskNotifyGive(s_raTask);
  }
}

static void raEnable(uint32_t totalSectors) {
  xSemaphoreTake(s_raGate, portMAX_DELAY);
  s_raTotal = totalSectors;
  s_raValid = false;
  s_raHits = 0;
  s_raMisses = 0;
  s_raEnabled = true;
  xSemaphoreGive(s_raGate);
}

static void raDisable() {
  xSemaphoreTake(s_raGate, portMAX_DELAY);
  if (s_raEnabled && (s_raHits || s_raMisses)) {
    Serial.printf("[sd] read-ahead: %u hits, %u misses\n",
                  (unsigned)s_raHits, (unsigned)s_raMisses);
  }
  s_raEnabled = false;
  raWaitIdle();
  s_raValid = false;
  s_raTotal = 0;
  xSemaphoreGive(s_raGate);
}

static int32_t onRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
  HostIoGuard io;
  if (!io.ready()) return -1;
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize || !buffer || !bufsize) return -1;

  uint8_t *out = (uint8_t *)buffer;
  // FatFs drive 0 is the SD card; LittleFS is a separate VFS.
  const BYTE pdrv = 0;

  raWaitIdle();

  if (offset == 0 && (bufsize % secSize) == 0) {
    uint32_t count = bufsize / secSize;
    if (s_raValid && lba == s_raLba && count <= s_raCount) {
      memcpy(out, s_raBuf, bufsize);
      s_raHits++;
    } else {
      if (disk_read(pdrv, out, lba, count) != RES_OK) return -1;
      s_raMisses++;
    }
    raStartLocked(lba + count, secSize);
    return bufsize;
  }

  uint32_t skip = offset;
  uint32_t left = bufsize;
  uint32_t sec = lba;
  uint8_t tmp[512];
  if (secSize > sizeof(tmp)) return -1;
  while (left) {
    if (disk_read(pdrv, tmp, sec, 1) != RES_OK) return -1;
    uint32_t n = secSize - skip;
    if (n > left) n = left;
    memcpy(out, tmp + skip, n);
    out += n;
    left -= n;
    skip = 0;
    sec++;
  }
  return bufsize;
}

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
  HostIoGuard io;
  if (!io.ready()) return -1;
  uint32_t secSize = SD_MMC.sectorSize();
  if (!secSize || !buffer || !bufsize) return -1;

  const BYTE pdrv = 0;

  // The write may land on the sectors the read-ahead holds.
  raWaitIdle();
  s_raValid = false;

  if (offset == 0 && (bufsize % secSize) == 0) {
    if (disk_write(pdrv, buffer, lba, bufsize / secSize) != RES_OK) return -1;
    disk_ioctl(pdrv, CTRL_SYNC, nullptr);
    return bufsize;
  }

  uint32_t skip = offset;
  uint32_t left = bufsize;
  uint32_t sec = lba;
  uint8_t tmp[512];
  if (secSize > sizeof(tmp)) return -1;
  while (left) {
    uint32_t n = secSize - skip;
    if (n > left) n = left;
    if (skip || n < secSize) {
      if (disk_read(pdrv, tmp, sec, 1) != RES_OK) return -1;
    }
    memcpy(tmp + skip, buffer, n);
    if (disk_write(pdrv, tmp, sec, 1) != RES_OK) return -1;
    buffer += n;
    left -= n;
    skip = 0;
    sec++;
  }
  disk_ioctl(pdrv, CTRL_SYNC, nullptr);
  return bufsize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
  (void)power_condition;
  (void)start;
  (void)load_eject;
  return true;
}

struct MountAttempt {
  bool mode1bit;
  int frequency;
  const char *label;
};

static const MountAttempt MOUNT_ATTEMPTS[] = {
  {false, 20000, "4-bit, 20 MHz"},
  {false, BOARD_MAX_SDMMC_FREQ, "4-bit, high-speed"},
  {true, 20000, "1-bit, 20 MHz"},
  {true, 10000, "1-bit, 10 MHz"},
};

// No SD_MMC.end() on success: tearing the mount down left the card
// unidentifiable until a power cycle. format_if_mount_failed stays false.
static bool mountCardLocked() {
  for (const MountAttempt &a : MOUNT_ATTEMPTS) {
    bool pinsOk = a.mode1bit
      ? SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN)
      : SD_MMC.setPins(SD_MMC_CLK_PIN, SD_MMC_CMD_PIN, SD_MMC_D0_PIN,
                       SD_MMC_D1_PIN, SD_MMC_D2_PIN, SD_MMC_D3_PIN);
    if (!pinsOk) {
      Serial.println("[sd] setPins refused");
      break;
    }
    bool mounted = SD_MMC.begin("/sdcard", a.mode1bit, false, a.frequency, 5);
    int type = SD_MMC.cardType();
    Serial.printf("[sd] try %-22s mount=%d type=%d\n", a.label, mounted, type);

    if (mounted && type != CARD_NONE) {
      s_cardPresent = true;
      s_sizeMB = SD_MMC.cardSize() / (1024 * 1024);
      // Capture automatic reset reason before MSC can expose this filesystem.
      // Remounts in this same boot do not create duplicate boot records.
      recordBootLocked();
      Serial.printf("[sd] mounted: %s, %llu MB\n", a.label, s_sizeMB);
      return true;
    }
    SD_MMC.end();
  }

  Serial.println("[sd] every mount attempt failed (card not identified)");
  s_cardPresent = false;
  s_exposed = false;
  MSC.mediaPresent(false);
  return false;
}

// begin() returns immediately when a card handle is already live, so a
// genuine remount has to tear the mount down first.
static bool remountLocked() {
  SD_MMC.end();
  return mountCardLocked();
}

static void publishGeometryLocked() {
  MSC.vendorID("LilyGO");
  MSC.productID(s_deviceName.c_str());
  MSC.productRevision("1.0");
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.onStartStop(onStartStop);
  MSC.begin(SD_MMC.numSectors(), SD_MMC.sectorSize());
}

void usbDriveBegin(bool exposed, const String &deviceName) {
  s_requestedExposure = exposed;
  s_lock = xSemaphoreCreateMutex();
  s_raGate = xSemaphoreCreateMutex();
  s_raIdle = xSemaphoreCreateBinary();
  xSemaphoreGive(s_raIdle);
  // Below the usbd task, above everything else the firmware runs, so the
  // card read is not left waiting behind the web server or the main loop.
  xTaskCreate(raTask, "sdahead", 4096, nullptr, configMAX_PRIORITIES - 3, &s_raTask);
  if (!deviceName.isEmpty()) s_deviceName = deviceName;

  lockSd();
  bool ok = mountCardLocked();
  if (ok) {
    publishGeometryLocked();
    s_exposed = exposed && s_cardPresent;
    if (s_exposed) raEnable(SD_MMC.numSectors());
    MSC.mediaPresent(s_exposed);
    Serial.printf("USB drive: %llu MB card, exposed=%d\n", s_sizeMB, s_exposed);
  } else {
    Serial.println("USB drive: no usable card, the reader will report empty.");
  }
  unlockSd();

  xTaskCreatePinnedToCore(cardWatchTask, "sdwatch", 8192, nullptr, 1, nullptr, 0);
}

static void cardWatchTask(void *arg) {
  (void)arg;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(15000));

    lockSd();
    if (!s_cardPresent && !s_exposed) {
      Serial.println("[sd] no card, retrying mount");
      if (mountCardLocked()) {
        publishGeometryLocked();
        if (s_requestedExposure) {
          raEnable(SD_MMC.numSectors());
          s_exposed = true;
          MSC.mediaPresent(true);
        }
        Serial.println("[sd] card picked up after boot");
      }
    }
    unlockSd();
  }
}

void usbDriveSetDeviceName(const String &name) {
  if (name.isEmpty()) return;
  lockSd();
  s_deviceName = name;
  MSC.productID(s_deviceName.c_str());
  unlockSd();
}

String usbDriveGetDeviceName() {
  lockSd();
  String n = s_deviceName;
  unlockSd();
  return n;
}

void usbDriveSetExposed(bool exposed) {
  lockSd();
  s_requestedExposure = exposed;
  if (s_cardPresent && s_exposed == exposed) { unlockSd(); return; }
  Serial.printf("[sd] setExposed(%d) from exposed=%d present=%d\n",
                exposed, s_exposed, s_cardPresent);

  bool wasExposed = s_exposed;

  // Media goes away first so the host is not reading across the remount.
  s_exposed = false;
  MSC.mediaPresent(false);
  raDisable();

  // Host writes bypass FatFs, so host -> firmware requires a fresh mount.
  // Firmware -> host only needs a sync after files close; tearing the card
  // down again adds an unnecessary chance of failing to identify it.
  bool mounted = s_cardPresent ? (!wasExposed || remountLocked()) : mountCardLocked();
  if (mounted) {
    disk_ioctl(0, CTRL_SYNC, nullptr);
    publishGeometryLocked();
    if (exposed) {
      s_exposed = true;
      raEnable(SD_MMC.numSectors());
      MSC.mediaPresent(true);
    }
  }
  unlockSd();
}

bool usbDriveExposureRequested() {
  lockSd();
  bool requested = s_requestedExposure;
  unlockSd();
  return requested;
}

// readRAW() needs the mount alive, so host and firmware cannot be separated
// by unmounting. Exclusive access is a rule enforced here instead.
static bool fsAvailableLocked() {
  if (s_exposed) return false;
  if (s_cardPresent) return true;
  Serial.println("[sd] card marked absent, retrying mount");
  return mountCardLocked();
}

bool usbDriveFsAvailable() {
  lockSd();
  bool ok = fsAvailableLocked();
  unlockSd();
  return ok;
}

// Rotate the diagnostic log past this size; refuse to export a larger coredump.
static constexpr size_t kDiagnosticLogMaxBytes = 512 * 1024;
static constexpr size_t kCoreDumpMaxBytes = 256 * 1024;

static bool appendDiagnosticLocked(const char *data, size_t size, bool newSession) {
  if (s_exposed || !s_cardPresent) return false;
  const char *directory = "/FLEABYTE-DIAGNOSTICS";
  const char *path = "/FLEABYTE-DIAGNOSTICS/hotspot.log";
  const char *previous = "/FLEABYTE-DIAGNOSTICS/hotspot.previous.log";
  if (!SD_MMC.exists(directory) && !SD_MMC.mkdir(directory)) return false;
  File existing = SD_MMC.open(path, "r");
  bool rotate = existing && (newSession || existing.size() + size > kDiagnosticLogMaxBytes);
  existing.close();
  if (rotate) {
    if ((SD_MMC.exists(previous) && !SD_MMC.remove(previous)) || !SD_MMC.rename(path, previous)) {
      return false;
    }
  }
  bool first = !SD_MMC.exists(path);
  File file = SD_MMC.open(path, FILE_APPEND);
  bool ok = bool(file);
  if (ok && first) ok = file.println("FLEABYTE_DIAGNOSTICS_V1") > 0;
  if (ok) ok = file.write(reinterpret_cast<const uint8_t *>(data), size) == size;
  file.flush(); file.close();
  return ok;
}

static void exportCrashLocked() {
  const esp_partition_t *partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
  size_t address = 0, size = 0;
  if (!partition || esp_core_dump_image_get(&address, &size) != ESP_OK) return;
  if (address != partition->address || size < 24 || size > partition->size || size > kCoreDumpMaxBytes ||
      esp_core_dump_image_check() != ESP_OK) return;
  // Export in normal boot, never in the panic handler or while MSC owns SD.
  // Keep flash intact so a failed SD write cannot destroy the evidence.
  const char *temporary = "/FLEABYTE-DIAGNOSTICS/panic.tmp";
  const char *path = "/FLEABYTE-DIAGNOSTICS/panic.bin";
  File file = SD_MMC.open(temporary, FILE_WRITE);
  uint8_t block[512];
  bool ok = bool(file);
  for (size_t offset = 0; ok && offset < size;) {
    size_t count = std::min(sizeof(block), size - offset);
    ok = esp_partition_read(partition, offset, block, count) == ESP_OK && file.write(block, count) == count;
    offset += count;
  }
  file.flush(); file.close();
  if (ok && (!SD_MMC.exists(path) || SD_MMC.remove(path))) ok = SD_MMC.rename(temporary, path);
  char line[100];
  int length = snprintf(line, sizeof(line), "%lu CORE_DUMP bytes=%u saved=%d file=panic.bin\n",
    (unsigned long)millis(), unsigned(size), ok);
  appendDiagnosticLocked(line, size_t(length), false);
}

static void recordBootLocked() {
  if (s_bootRecorded) return;
  char hash[65] = {}, line[200];
  // The SDK helper intentionally returns only CONFIG_APP_RETRIEVE_LEN_ELF_SHA
  // characters (nine in this core). Store all bytes from the app descriptor.
  const auto *description = esp_app_get_description();
  const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(description->app_elf_sha256); ++i) {
    hash[i * 2] = hex[description->app_elf_sha256[i] >> 4];
    hash[i * 2 + 1] = hex[description->app_elf_sha256[i] & 15];
  }
  int size = snprintf(line, sizeof(line), "%lu BOOT firmware=%s %s reset=%d elf=%s\n",
    (unsigned long)millis(), __DATE__, __TIME__, int(esp_reset_reason()), hash);
  if (size > 0 && size < int(sizeof(line))) {
    s_bootRecorded = appendDiagnosticLocked(line, size_t(size), false);
    if (s_bootRecorded) exportCrashLocked();
  }
}

bool usbDriveDiagnosticAppend(const char *data, size_t size, bool newSession) {
  lockSd();
  bool ok = appendDiagnosticLocked(data, size, newSession);
  unlockSd();
  return ok;
}

bool usbDrivePathIsSafe(const String &path) {
  if (path.isEmpty() || path[0] != '/') return false;
  if (path.indexOf("..") >= 0) return false;
  return true;
}

bool usbDriveList(const String &path, std::vector<SdEntry> &out) {
  if (!usbDrivePathIsSafe(path)) {
    Serial.printf("[sd] list %s refused: unsafe path\n", path.c_str());
    return false;
  }

  lockSd();
  if (!fsAvailableLocked()) {
    Serial.printf("[sd] list %s refused: exposed=%d present=%d\n",
                  path.c_str(), s_exposed, s_cardPresent);
    unlockSd();
    return false;
  }

  File dir = SD_MMC.open(path);
  if (!dir || !dir.isDirectory()) {
    Serial.printf("[sd] list %s: not a directory\n", path.c_str());
    if (dir) dir.close();
    unlockSd();
    return false;
  }

  // A filesystem the host rewrote can come back inconsistent. These caps
  // make that show up as a short listing instead of an allocation on a
  // nonsense length, which reboots the device.
  const size_t MAX_ENTRIES = 256;
  File f = dir.openNextFile();
  while (f && out.size() < MAX_ENTRIES) {
    String n = String(f.name());
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (!n.isEmpty() && n.length() <= 255) {
      out.push_back(SdEntry{n, (uint32_t)f.size(), f.isDirectory()});
    }
    f.close();
    f = dir.openNextFile();
  }
  if (f) f.close();
  dir.close();
  unlockSd();

  Serial.printf("[sd] list %s -> %u entries\n", path.c_str(), (unsigned)out.size());
  return true;
}

File usbDriveOpen(const String &path) {
  if (!usbDrivePathIsSafe(path)) return File();
  lockSd();
  File f = fsAvailableLocked() ? SD_MMC.open(path, "r") : File();
  unlockSd();
  return f;
}

bool usbDriveDelete(const String &path) {
  if (!usbDrivePathIsSafe(path)) {
    Serial.printf("[sd] delete %s refused: unsafe path\n", path.c_str());
    return false;
  }

  lockSd();
  if (!fsAvailableLocked()) {
    unlockSd();
    Serial.printf("[sd] delete %s refused\n", path.c_str());
    return false;
  }

  File f = SD_MMC.open(path);
  if (!f) {
    unlockSd();
    Serial.printf("[sd] delete %s: not found\n", path.c_str());
    return false;
  }
  bool isDir = f.isDirectory();
  f.close();
  bool ok = isDir ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
  unlockSd();

  Serial.printf("[sd] delete %s dir=%d -> %d\n", path.c_str(), isDir, ok);
  return ok;
}

UsbDriveStatus usbDriveGetStatus() {
  lockSd();
  UsbDriveStatus s{s_cardPresent, s_exposed, s_sizeMB};
  unlockSd();
  return s;
}
