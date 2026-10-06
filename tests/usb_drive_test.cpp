// Include production storage code so the fake MSC host can invoke the actual
// sector callbacks and the watch task can run a single simulated retry.
#include "../mitebyte/usb_drive.cpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstring>

struct FakeSemaphore { bool available = false; std::mutex mutex; std::condition_variable cv; };
SemaphoreHandle_t xSemaphoreCreateBinary() { return new FakeSemaphore; }
SemaphoreHandle_t xSemaphoreCreateMutex() { auto *s = new FakeSemaphore; s->available = true; return s; }
int xSemaphoreGive(SemaphoreHandle_t s) {
  std::lock_guard<std::mutex> lock(s->mutex); s->available = true; s->cv.notify_all(); return 1;
}
int xSemaphoreTake(SemaphoreHandle_t s, uint32_t) {
  std::unique_lock<std::mutex> lock(s->mutex); s->cv.wait(lock, [&] { return s->available; }); s->available = false; return 1;
}
struct EndFakeTask {};
static thread_local int notifications = 0, watchDelays = 0;
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *handle) {
  *handle = reinterpret_cast<void *>(1); return 1;
}
int xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *, unsigned) { return 1; }
void xTaskNotifyGive(TaskHandle_t) {
  notifications = 1;
  try { raTask(nullptr); } catch (EndFakeTask &) {}
}
uint32_t ulTaskNotifyTake(int, uint32_t) { if (notifications-- > 0) return 1; throw EndFakeTask{}; }
void vTaskDelay(uint32_t) { if (++watchDelays > 1) throw EndFakeTask{}; }

static std::atomic<bool> allowMount{true}, mounted{false}, media{false};
static std::atomic<unsigned> endCalls{0}, activeDisk{0}, diskCalls{0};
static std::mutex blockMutex;
static std::condition_variable blockCv;
static bool blockNext = false, entered = false, releaseDisk = false;
static std::vector<std::string> records;
static std::vector<uint8_t> crash(1401), exportedCrash;
static esp_partition_t crashPartition{0xBD0000, 256 * 1024};
static bool crashValid = false, failCrashRead = false, publishedCrash = false;
static size_t declaredCrashSize = crash.size();
const esp_partition_t *esp_partition_find_first(int, int, const char *) { return &crashPartition; }
int esp_core_dump_image_get(size_t *address, size_t *size) {
  *address = crashPartition.address; *size = declaredCrashSize; return 0;
}
int esp_core_dump_image_check() { return crashValid ? 0 : -1; }
int esp_partition_read(const esp_partition_t *, size_t offset, void *data, size_t size) {
  assert(!media && activeDisk == 0);
  if (failCrashRead) return -1;
  assert(offset + size <= crash.size()); std::memcpy(data, crash.data() + offset, size); return 0;
}
bool FakeSd::rename(const char *from, const char *to) {
  if (std::strcmp(from, "/MITEBYTE-DIAGNOSTICS/panic.tmp") || std::strcmp(to, "/MITEBYTE-DIAGNOSTICS/panic.bin")) return false;
  publishedCrash = true; return true;
}
uint32_t millis() { return 42; }
size_t File::write(const uint8_t *data, size_t size) {
  assert(!media && activeDisk == 0); // boot record precedes host access
  if (!std::strcmp(path, "/MITEBYTE-DIAGNOSTICS/panic.tmp")) {
    exportedCrash.insert(exportedCrash.end(), data, data + size); return size;
  }
  records.emplace_back(reinterpret_cast<const char *>(data), size); return size;
}
bool FakeSd::begin(const char *, bool, bool, int, int) { mounted = allowMount.load(); return mounted; }
void FakeSd::end() { assert(activeDisk == 0); mounted = false; ++endCalls; }
int FakeSd::cardType() { return mounted ? 1 : CARD_NONE; }
uint32_t FakeSd::sectorSize() { return mounted ? 512 : 0; }
void USBMSC::mediaPresent(bool value) { media = value; }
static void diskOperation() {
  assert(mounted); ++activeDisk; ++diskCalls;
  std::unique_lock<std::mutex> lock(blockMutex);
  if (blockNext) {
    blockNext = false; entered = true; blockCv.notify_all();
    blockCv.wait(lock, [] { return releaseDisk; });
  }
  assert(mounted); --activeDisk;
}
extern "C" DRESULT disk_read(BYTE, BYTE *buffer, uint32_t, uint32_t count) {
  diskOperation(); std::memset(buffer, 0x5a, count * 512); return RES_OK;
}
extern "C" DRESULT disk_write(BYTE, const BYTE *, uint32_t, uint32_t) { diskOperation(); return RES_OK; }
extern "C" DRESULT disk_ioctl(BYTE, BYTE, void *) { assert(mounted); return RES_OK; }

int main() {
  usbDriveBegin(true, "test"); assert(media && usbDriveGetStatus().exposed);
  assert(records.size() == 1 && records[0].find(" BOOT firmware=") != std::string::npos);
  assert(records[0].find("reset=4 elf=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n") != std::string::npos);
  usbDriveSetExposed(false);
  crashValid = true;
  for (size_t i = 0; i < crash.size(); ++i) crash[i] = uint8_t(i);
  exportCrashLocked();
  assert(publishedCrash && exportedCrash == crash);
  assert(records.back().find("CORE_DUMP bytes=1401 saved=1") != std::string::npos);
  // Reject corrupt/oversized dumps before reading or publishing a file.
  publishedCrash = false; exportedCrash.clear(); crashValid = false;
  exportCrashLocked(); assert(exportedCrash.empty() && !publishedCrash);
  crashValid = true; declaredCrashSize = 256 * 1024 + 1;
  exportCrashLocked(); assert(exportedCrash.empty() && !publishedCrash);
  declaredCrashSize = crash.size(); failCrashRead = true;
  exportCrashLocked(); assert(!publishedCrash);
  assert(records.back().find("saved=0") != std::string::npos);
  failCrashRead = false; crashValid = false;
  usbDriveSetExposed(true);
  uint8_t buffer[512] = {};
  for (bool write : {false, true}) {
    { std::lock_guard<std::mutex> lock(blockMutex); blockNext = true; entered = false; releaseDisk = false; }
    unsigned before = endCalls;
    std::thread host([&] {
      assert((write ? onWrite(0, 0, buffer, sizeof(buffer)) : onRead(0, 0, buffer, sizeof(buffer))) == sizeof(buffer));
    });
    {
      std::unique_lock<std::mutex> lock(blockMutex);
      assert(blockCv.wait_for(lock, std::chrono::seconds(2), [] { return entered; }));
    }
    std::atomic<bool> revoked{false};
    std::thread handoff([&] { usbDriveSetExposed(false); revoked = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(!revoked && endCalls == before); // remount waits for the in-flight host request
    { std::lock_guard<std::mutex> lock(blockMutex); releaseDisk = true; blockCv.notify_all(); }
    host.join(); handoff.join();
    assert(revoked && !media && endCalls == before + 1);
    unsigned calls = diskCalls;
    assert(onRead(0, 0, buffer, sizeof(buffer)) == -1);
    assert(onWrite(0, 0, buffer, sizeof(buffer)) == -1);
    assert(diskCalls == calls); // late callbacks must not touch firmware-owned media
    usbDriveSetExposed(true);
    assert(media && endCalls == before + 1); // no needless second SD teardown
  }
  // A failed identification does not discard the user's requested exposure.
  allowMount = false; usbDriveSetExposed(false);
  assert(!usbDriveGetStatus().cardPresent && !media);
  usbDriveSetExposed(true); assert(usbDriveExposureRequested() && !media);
  allowMount = true; watchDelays = 0;
  try { cardWatchTask(nullptr); } catch (EndFakeTask &) {}
  assert(usbDriveGetStatus().cardPresent && usbDriveGetStatus().exposed && media);
  assert(onRead(0, 0, buffer, sizeof(buffer)) == sizeof(buffer));
  size_t bootRecords = 0;
  for (const auto &record : records) if (record.find(" BOOT firmware=") != std::string::npos) ++bootRecords;
  assert(bootRecords == 1); // remount and retry must not invent another reset
  puts("PASS: production SD read/write handoff, late MSC callbacks, restore without remount and mount retry");
}
