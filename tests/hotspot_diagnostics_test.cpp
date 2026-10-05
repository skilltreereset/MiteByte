#include "../fleabyte/src/tools/hotspot_diagnostics.h"
#include "../fleabyte/usb_drive.h"
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstring>
#include <chrono>
struct FakeQueue {
  unsigned capacity; size_t size; std::deque<std::vector<char>> records;
  std::mutex mutex; std::condition_variable cv;
};
struct FakeSemaphore { bool value=false; std::mutex mutex; std::condition_variable cv; };
QueueHandle_t xQueueCreate(unsigned count,size_t size) { auto *q=new FakeQueue; q->capacity=count;q->size=size;return q; }
int xQueueSend(QueueHandle_t q,const void *data,uint32_t timeout) {
  std::unique_lock<std::mutex> lock(q->mutex);
  if (!timeout && q->records.size()==q->capacity) return 0;
  q->cv.wait(lock,[&]{return q->records.size()<q->capacity;});
  auto *bytes=static_cast<const char *>(data);q->records.emplace_back(bytes,bytes+q->size);q->cv.notify_all();return 1;
}
int xQueueReceive(QueueHandle_t q,void *data,uint32_t) {
  std::unique_lock<std::mutex> lock(q->mutex);q->cv.wait(lock,[&]{return !q->records.empty();});
  memcpy(data,q->records.front().data(),q->size);q->records.pop_front();q->cv.notify_all();return 1;
}
SemaphoreHandle_t xSemaphoreCreateBinary() { return new FakeSemaphore; }
int xSemaphoreGive(SemaphoreHandle_t s) { std::lock_guard<std::mutex> lock(s->mutex);s->value=true;s->cv.notify_all();return 1; }
int xSemaphoreTake(SemaphoreHandle_t s,uint32_t) {
  std::unique_lock<std::mutex> lock(s->mutex);s->cv.wait(lock,[&]{return s->value;});s->value=false;return 1;
}
int xTaskCreate(void(*task)(void*),const char*,unsigned,void *arg,unsigned,TaskHandle_t *handle) {
  *handle=reinterpret_cast<void*>(1);std::thread(task,arg).detach();return 1;
}
void vTaskDelay(uint32_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
uint32_t millis() { static std::atomic<uint32_t> now{0};return ++now; }
static std::atomic<bool> exposed{true},available{true},writeFails{false};
static std::mutex savedMutex;static std::vector<std::string> saved;static unsigned sessions=0;
static std::thread::id mainThread;
UsbDriveStatus usbDriveGetStatus() { return {true,exposed.load(),1024}; }
bool usbDriveExposureRequested() { return exposed.load(); }
void usbDriveSetExposed(bool value) { exposed=value; }
bool usbDriveFsAvailable() { return available; }
bool usbDriveDiagnosticAppend(const char *data,size_t size,bool newSession) {
  assert(!exposed && std::this_thread::get_id()!=mainThread);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::lock_guard<std::mutex> lock(savedMutex);
  if(newSession) ++sessions;
  saved.emplace_back(data,size);
  return !writeFails;
}
int main() {
  mainThread=std::this_thread::get_id();
  assert(hotspotDiagnosticsBegin() && !exposed);
  std::vector<std::thread> producers;
  for(unsigned n=0;n<4;++n) producers.emplace_back([n]{for(unsigned i=0;i<300;++i)hotspotDiagnosticLog("test producer=%u record=%u",n,i);});
  for(auto &p:producers)p.join();
  assert(hotspotDiagnosticsDropped()>0); // producers must not wait for slow SD
  hotspotDiagnosticsEnd();assert(exposed && sessions==1);
  assert(saved.back().find("SESSION_END")!=std::string::npos);
  auto count=saved.size();hotspotDiagnosticLog("after stop");vTaskDelay(5);assert(saved.size()==count);
  assert(hotspotDiagnosticsBegin());hotspotDiagnosticLog("second session");hotspotDiagnosticsEnd();
  assert(exposed && sessions==2 && saved.back().find("SESSION_END")!=std::string::npos);
  available=false;assert(!hotspotDiagnosticsBegin() && exposed);hotspotDiagnosticsEnd();assert(exposed);
  available=true;exposed=false;writeFails=true;assert(hotspotDiagnosticsBegin());hotspotDiagnosticsEnd();
  assert(!exposed && std::string(hotspotDiagnosticsState())=="SD write unavailable");
  puts("PASS: production async diagnostics, concurrent producers, bounded queue, flush before host ownership, restarts and SD failures");
}
