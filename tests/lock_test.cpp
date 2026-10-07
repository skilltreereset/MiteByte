// Runs the production lock, gesture validation and display code with fake IO.
#include "config.h"
#include "lock.h"
#include "storage.h"
#include "ui_display.h"
#include <cassert>
#include <iostream>
#include <vector>

static uint32_t now = 0;
static int button = HIGH;
static int backlight = 255;
static int pwmAttachments = 0;
static int armedCount = 0;
static bool storageCanArm = true;
static int resets = 0;
static int ledData = 0;
static std::vector<int> ledBits;
TestESP ESP;
uint32_t millis() { return now; }
void delay(uint32_t ms) { now += ms; }
int digitalRead(int) { return button; }
void pinMode(int, int) {}
bool ledcAttach(int, int, int) { ++pwmAttachments; return true; }
void ledcWrite(int, int duty) { backlight = duty; }
void digitalWrite(int pin, int value) {
  if (pin == TFT_BL) backlight = value == HIGH ? 255 : 0;
  if (pin == LED_DI_PIN) ledData = value;
  if (pin == LED_CI_PIN && value == HIGH) ledBits.push_back(ledData);
}
bool storageHardlockArm(uint8_t count) {
  if (!storageCanArm) return false;
  armedCount = count;
  return true;
}
void storageResetSettings() { ++resets; }
void TestESP::restart() { throw resets; }

static void begin(bool pending = false, bool enabled = true, uint8_t count = 1,
                  const char *unlock = DEFAULT_UNLOCK_SEQUENCE,
                  const char *hard = DEFAULT_HARDLOCK_SEQUENCE) {
  button = HIGH;
  armedCount = 0;
  lockBegin(unlock, hard, LOCK_LONG_PRESS_MS, pending, enabled, count);
}

static LockEvent press(uint32_t duration) {
  button = LOW;
  lockTick();
  now += duration;
  lockTick();
  button = HIGH;
  auto event = lockTick();
  now += 50;
  return event;
}

static LockEvent gesture(const char *seq) {
  LockEvent event = LOCK_EVT_NONE;
  for (; *seq; ++seq) event = press(*seq == 'S' ? 80 : 300);
  return event;
}

static void online() {
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_UNLOCKED);
  displaySetScreenLocked(false); // goOnline() does this in the sketch
}

int main() {
  assert(storageLockSequencesAreCompatible("SSSLL", "SSSSS"));
  for (const auto &pair : std::vector<std::pair<String, String>>{
         {"S", "S"}, {"SSSLL", "SSS"}, {"LSSLL", "SS"},
         {"LLSS", "SS"}, {"SS", "LSSL"}, {"", "L"}, {"SX", "LL"}}) {
    assert(!storageLockSequencesAreCompatible(pair.first, pair.second));
  }
  assert(!storageLockSequenceIsValid("SSSSSSSSSSSSS"));
  assert(storageLockSequenceIsValid("SSSSSSSSSSSS"));

  displayBegin();
  displaySetScreenOn(true);
  displaySetScreenBright(100);
  displayWake();
  assert(backlight == 255 && pwmAttachments == 0);
  begin();
  online();
  assert(backlight == 0);

  // Between the tap and the screen-lock hold sits the menu hold.
  assert(press(MENU_HOLD_MS - 100) == LOCK_EVT_TAP_ONLINE);
  assert(press(MENU_HOLD_MS) == LOCK_EVT_HOLD_ONLINE);
  assert(press(SCREEN_LOCK_ENTER_HOLD_MS - 100) == LOCK_EVT_HOLD_ONLINE);
  assert(!lockScreenIsLocked());

  // The hold is readable while it happens (the menu fills its frame from it),
  // and reads 0 with the button up.
  assert(lockHeldMs() == 0);
  button = LOW;
  lockTick();
  now += 400;
  lockTick();
  assert(lockHeldMs() == 400);
  now += 700;
  assert(lockHeldMs() == 1100);
  button = HIGH;
  lockTick();
  assert(lockHeldMs() == 0);
  now += 50;

  lockRequestScreenLock();
  assert(lockScreenIsLocked() && backlight == 255);
  // The exact setters used by both preview and Apply must respect the lock.
  displaySetScreenOn(true);
  displaySetScreenBright(40);
  displayWake();
  assert(backlight == 255);
  assert(lockRequestScreenUnlock());
  assert(backlight == 153); // latest 40% preference, not the pre-lock value

  lockRequestScreenLock();
  displaySetScreenOn(false);
  assert(lockRequestScreenUnlock());
  assert(backlight == 255 && !displayGetScreenOn());
  displayWake();
  assert(backlight == 153);
  lockRequestScreenLock();
  assert(backlight == 255); // cancels the temporary wake immediately
  assert(lockRequestScreenUnlock());
  assert(backlight == 255); // stale wake does not return on unlock

  // Holding to lock must not also become the first long gesture press.
  begin(false, false, 1, "L", "S");
  assert(gesture("L") == LOCK_EVT_UNLOCKED);
  displaySetScreenLocked(false);
  press(SCREEN_LOCK_ENTER_HOLD_MS);
  assert(lockScreenIsLocked());
  assert(gesture("L") == LOCK_EVT_SCREEN_LOCK_EXITED);

  begin(false, true, 3);
  online();
  lockRequestScreenLock();
  displaySetLed(true);
  now += 1000;
  displayTick();
  now += 1000;
  displayTick();
  now += 1000;
  displayTick();
  ledBits.clear();
  assert(gesture(DEFAULT_HARDLOCK_SEQUENCE) == LOCK_EVT_HARD_LOCKED);
  assert(lockIsLocked() && !lockScreenIsLocked() && armedCount == 3);
  assert(backlight == 255 && !lockRequestScreenUnlock());
  assert(ledBits.size() == 96);
  // APA102's final B/G/R bytes must be black immediately, without a fade tick.
  for (size_t i = 40; i < 64; ++i) assert(ledBits[i] == 0);
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_NONE);
  assert(armedCount == 3); // subsequent gestures cannot re-arm the counter

  begin();
  assert(gesture(DEFAULT_HARDLOCK_SEQUENCE) == LOCK_EVT_HARD_LOCKED);
  assert(lockIsLocked());
  begin(true);
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_NONE);
  assert(armedCount == 0);
  begin(false, false);
  assert(gesture(DEFAULT_HARDLOCK_SEQUENCE) == LOCK_EVT_NONE);
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_UNLOCKED);

  // The explicit UI action works even when the gesture is disabled. It must
  // allow the HTTP response to finish before the main loop shuts down Wi-Fi.
  assert(lockRequestHardLock(2));
  assert(!lockIsLocked() && armedCount == 2);
  assert(!lockRequestHardLock(2));
  assert(lockTick() == LOCK_EVT_HARD_LOCKED);
  assert(lockIsLocked() && backlight == 255 && armedCount == 2);
  assert(lockTick() == LOCK_EVT_NONE);

  begin();
  online();
  lockRequestScreenLock();
  assert(lockRequestHardLock(4));
  assert(!lockRequestScreenUnlock());
  assert(lockTick() == LOCK_EVT_HARD_LOCKED);
  assert(lockIsLocked() && armedCount == 4);

  // A failed flash write must leave the UI connected and the screen unlockable.
  begin();
  online();
  lockRequestScreenLock();
  storageCanArm = false;
  assert(!lockRequestHardLock(2));
  assert(lockTick() == LOCK_EVT_NONE && lockScreenIsLocked());
  assert(lockRequestScreenUnlock());
  begin();
  assert(gesture(DEFAULT_HARDLOCK_SEQUENCE) == LOCK_EVT_NONE);
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_UNLOCKED);
  storageCanArm = true;

  // Disabling startup standby starts online; pending hard lock overrides it.
  lockBegin(DEFAULT_UNLOCK_SEQUENCE, DEFAULT_HARDLOCK_SEQUENCE, LOCK_LONG_PRESS_MS,
            false, true, 1, false);
  assert(!lockIsLocked());
  lockRequestScreenLock();
  assert(lockScreenIsLocked());
  lockBegin(DEFAULT_UNLOCK_SEQUENCE, DEFAULT_HARDLOCK_SEQUENCE, LOCK_LONG_PRESS_MS,
            true, true, 1, false);
  assert(lockIsLocked());
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_NONE);
  assert(!lockRequestHardLock(2));

  // Previously persisted conflicting values recover to known defaults.
  begin(false, true, 1, "S", "S");
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_UNLOCKED);
  begin();
  assert(press(10) == LOCK_EVT_NONE); // contact noise is not a short press
  assert(gesture(DEFAULT_UNLOCK_SEQUENCE) == LOCK_EVT_UNLOCKED);

  begin(true);
  try { press(FACTORY_RESET_HOLD_MS); assert(false); }
  catch (int count) { assert(count == 1); }
  std::cout << "PASS: gesture validation, screen preferences, hard lock, reset\n";
}
