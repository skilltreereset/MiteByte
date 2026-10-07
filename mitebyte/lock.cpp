#include "lock.h"
#include "config.h"
#include "storage.h"
#include "ui_display.h"

enum LockMode : uint8_t { MODE_LOCKED, MODE_SCREEN_LOCK, MODE_ONLINE };

static LockMode s_mode = MODE_LOCKED;

// Two ways to be hard-locked: pending from a previous boot (survives
// reinsertion, cleared by storageHardlockTick counting down), or armed this
// session by the gesture. Either silently refuses the unlock sequence.
static bool s_hardlockPendingThisBoot = false;
static bool s_hardlocked = false;
static bool s_hardlockRequested = false;

static String s_unlockSeq;
static String s_hardlockSeq;
static uint16_t s_longPressMs;
static bool s_hardlockEnabled = true;
static uint8_t s_hardlockReinserts = HARDLOCK_REINSERTS_DEFAULT;

// Recent presses as a string of 'S'/'L', oldest first. Scrolls once full, so
// a sequence is matched against its tail: a mistake slides the window
// instead of resetting it.
static char s_buf[LOCK_MAX_PRESSES];
static uint8_t s_count = 0;

// Single shared button edge tracker: one physical button, read from whatever
// mode is active, plus the always-on reset watch.
static bool s_lastBtn = HIGH;
static uint32_t s_downAt = 0;
static bool s_resetArmed = false;
static bool s_screenLockArmed = false;

static void pushPress(char kind) {
  if (s_count < LOCK_MAX_PRESSES) {
    s_buf[s_count++] = kind;
    return;
  }
  for (uint8_t i = 1; i < LOCK_MAX_PRESSES; i++) s_buf[i - 1] = s_buf[i];
  s_buf[LOCK_MAX_PRESSES - 1] = kind;
}

static void resetBuffer() { s_count = 0; }

static bool tailMatches(const String &seq) {
  uint8_t n = seq.length();
  if (n == 0 || s_count < n) return false;
  uint8_t base = s_count - n;
  for (uint8_t i = 0; i < n; i++) {
    if (s_buf[base + i] != seq[i]) return false;
  }
  return true;
}

static bool isHardlocked() {
  return s_hardlockPendingThisBoot || s_hardlocked || s_hardlockRequested;
}

// Arm the hard lock in place: no reboot. The unlock sequence is refused from
// now on, and storageHardlockArm() persists it so a reinsertion stays locked
// too. Clearing it takes two reinsertions (see storageHardlockTick) or a
// factory reset.
static void activateHardlock() {
  s_hardlocked = true;
  s_mode = MODE_LOCKED;
  displayStandby();
  resetBuffer();
}

static bool armHardlock() {
  if (!storageHardlockArm(s_hardlockReinserts)) return false;
  activateHardlock();
  return true;
}

// Owns the full reboot: shown, written, then gone. Mirrors the hold-to-reset
// the dongle has always had, just no longer gated to the online state.
static void doFactoryReset() {
  displayShowMessage("FACTORY RESET", "restoring defaults");
  storageResetSettings();
  delay(1200);
  ESP.restart();
}

static void enterScreenLock() {
  s_mode = MODE_SCREEN_LOCK;
  resetBuffer();
  displaySetScreenLocked(true);
}

static void exitScreenLock() {
  s_mode = MODE_ONLINE;
  resetBuffer();
  displaySetScreenLocked(false);
}

void lockBegin(const String &unlockSequence, const String &hardlockSequence,
               uint16_t longPressMs, bool hardlockPendingThisBoot,
               bool hardlockEnabled, uint8_t hardlockReinserts, bool standbyOnBoot) {
  s_unlockSeq = storageLockSequenceIsValid(unlockSequence)
                    ? unlockSequence : String(DEFAULT_UNLOCK_SEQUENCE);
  s_hardlockSeq = storageLockSequenceIsValid(hardlockSequence)
                      ? hardlockSequence : String(DEFAULT_HARDLOCK_SEQUENCE);
  if (!storageLockSequencesAreCompatible(s_unlockSeq, s_hardlockSeq)) {
    s_unlockSeq = DEFAULT_UNLOCK_SEQUENCE;
    s_hardlockSeq = DEFAULT_HARDLOCK_SEQUENCE;
  }
  s_longPressMs = storageLongPressIsValid(longPressMs) ? longPressMs : LOCK_LONG_PRESS_MS;
  s_hardlockPendingThisBoot = hardlockPendingThisBoot;
  s_hardlockEnabled = hardlockEnabled;
  s_hardlockReinserts = hardlockReinserts;

  s_mode = (standbyOnBoot || hardlockPendingThisBoot) ? MODE_LOCKED : MODE_ONLINE;
  s_hardlocked = false;
  s_hardlockRequested = false;
  resetBuffer();
  s_lastBtn = digitalRead(BOOT_PIN);
  s_downAt = millis();
  s_resetArmed = false;
  s_screenLockArmed = false;

  displaySetScreenLocked(s_mode == MODE_LOCKED);
}

uint32_t lockHeldMs() { return s_lastBtn == LOW ? millis() - s_downAt : 0; }

bool lockIsLocked() { return s_mode == MODE_LOCKED; }
bool lockScreenIsLocked() { return s_mode == MODE_SCREEN_LOCK; }

void lockRequestScreenLock() {
  if (s_mode != MODE_ONLINE) return;
  enterScreenLock();
}

// Restore the screen preference from the web UI. Refused while hard-locked, so the
// web path cannot sidestep a hard lock. Returns true if it left SCREEN_LOCK.
bool lockRequestScreenUnlock() {
  if (s_mode != MODE_SCREEN_LOCK || isHardlocked()) return false;
  exitScreenLock();
  return true;
}

bool lockRequestHardLock(uint8_t reinserts) {
  if (s_mode == MODE_LOCKED || isHardlocked()) return false;
  if (!storageHardlockArm(reinserts)) return false;
  s_hardlockRequested = true;
  return true;
}

LockEvent lockTick() {
  if (s_hardlockRequested) {
    s_hardlockRequested = false;
    activateHardlock();
    return LOCK_EVT_HARD_LOCKED;
  }
  bool btn = digitalRead(BOOT_PIN);  // active low: LOW is pressed
  uint32_t now = millis();
  LockEvent ev = LOCK_EVT_NONE;

  if (s_lastBtn == HIGH && btn == LOW) {
    s_downAt = now;
    s_resetArmed = false;
    s_screenLockArmed = false;
  }

  if (btn == LOW) {
    uint32_t held = now - s_downAt;

    // Works in every mode, every time: the owner's escape hatch, including
    // out of a hard lock. Does not return.
    if (!s_resetArmed && held >= FACTORY_RESET_HOLD_MS) {
      s_resetArmed = true;
      doFactoryReset();
    }

    if (s_mode == MODE_ONLINE && !s_screenLockArmed && !s_resetArmed &&
        held >= SCREEN_LOCK_ENTER_HOLD_MS) {
      s_screenLockArmed = true;
      enterScreenLock();
      ev = LOCK_EVT_SCREEN_LOCK_ENTERED;
    }
  }

  if (s_lastBtn == LOW && btn == HIGH) {
    uint32_t held = now - s_downAt;
    bool consumedByHold = s_resetArmed || s_screenLockArmed;
    s_resetArmed = false;
    s_screenLockArmed = false;

    if (!consumedByHold && held >= LOCK_PRESS_DEBOUNCE_MS) {
      char kind = (held >= s_longPressMs) ? 'L' : 'S';

      switch (s_mode) {
        case MODE_LOCKED:
          // Hard-locked: every press is swallowed in silence. No unlock, no
          // re-arm, no feedback — the device just stays a plain drive.
          if (isHardlocked()) break;
          pushPress(kind);
          if (s_hardlockEnabled && tailMatches(s_hardlockSeq)) {
            if (armHardlock()) ev = LOCK_EVT_HARD_LOCKED;
          } else if (tailMatches(s_unlockSeq)) {
            s_mode = MODE_ONLINE;
            resetBuffer();
            ev = LOCK_EVT_UNLOCKED;
          }
          break;

        case MODE_SCREEN_LOCK:
          if (isHardlocked()) break;
          pushPress(kind);
          if (s_hardlockEnabled && tailMatches(s_hardlockSeq)) {
            if (armHardlock()) ev = LOCK_EVT_HARD_LOCKED;
          } else if (tailMatches(s_unlockSeq)) {
            exitScreenLock();
            ev = LOCK_EVT_SCREEN_LOCK_EXITED;
          }
          break;

        case MODE_ONLINE:
          // Anything held to SCREEN_LOCK_ENTER_HOLD_MS was consumed above.
          ev = (held >= MENU_HOLD_MS) ? LOCK_EVT_HOLD_ONLINE : LOCK_EVT_TAP_ONLINE;
          break;
      }
    }
  }

  s_lastBtn = btn;
  return ev;
}
