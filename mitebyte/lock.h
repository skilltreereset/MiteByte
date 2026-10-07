#pragma once
#include <Arduino.h>

// Button-driven lock states. See config.h for thresholds and sequences.
//
// LOCKED: boot default. Mass storage only, screen off, no radio/HID/web.
// An armed boot run can enable HID while the screen/radio stay locked.
// Exit via the unlock sequence -> ONLINE.
//
// ONLINE: unlocked, everything running normally.
//
// SCREEN_LOCK: entered from ONLINE (web call, or a 2 s hold on the button)
// without dropping anything — radio, HID and web keep running. Only the
// screen (off) and the button (captured for the unlock sequence only) are
// locked. Exit is the same unlock sequence as LOCKED.
//
// From LOCKED or SCREEN_LOCK, the hard-lock sequence arms a lockout that
// returns to storage-only standby and survives the configured number of
// reinsertions. A factory reset (10 s hold in every state) wipes it immediately.

enum LockEvent : uint8_t {
  LOCK_EVT_NONE,
  LOCK_EVT_UNLOCKED,            // LOCKED -> ONLINE: bring the dongle online
  LOCK_EVT_SCREEN_LOCK_ENTERED,
  LOCK_EVT_SCREEN_LOCK_EXITED,
  LOCK_EVT_HARD_LOCKED,         // stop services and return USB to storage only
  LOCK_EVT_TAP_ONLINE,          // a tap while ONLINE: show the join screen
  LOCK_EVT_HOLD_ONLINE,         // released after MENU_HOLD_MS while ONLINE
};

// Call once at boot, after settings are loaded and the display/USB drive are
// up. hardlockPendingThisBoot comes from storageHardlockTick(), called once
// at boot before this.
void lockBegin(const String &unlockSequence, const String &hardlockSequence,
               uint16_t longPressMs, bool hardlockPendingThisBoot,
               bool hardlockEnabled, uint8_t hardlockReinserts,
               bool standbyOnBoot = true);

// How long the current press has lasted, 0 while the button is up. The hold
// events fire only on release, so a screen that wants to show the hold as it
// happens (the menu's fill) reads it here.
uint32_t lockHeldMs();

// True while in insertion-lock or hard-lock standby.
bool lockIsLocked();

bool lockScreenIsLocked();

// Request to enter or leave SCREEN_LOCK from the web API. Entering is a no-op
// unless currently ONLINE; leaving restores the screen preference and is a no-op
// unless currently SCREEN_LOCK. Leaving is refused while hard-locked.
void lockRequestScreenLock();
bool lockRequestScreenUnlock();

// Explicit UI action works even when the hard-lock gesture is disabled.
// Persists first, then enters standby on the next tick so HTTP can respond.
bool lockRequestHardLock(uint8_t reinserts);

// Call every loop iteration, in every state. Drives the button: gesture
// capture in LOCKED/SCREEN_LOCK, the 2 s screen-lock hold and tap-to-wake in
// ONLINE, and the 10 s factory-reset hold in all three (which restarts the
// device directly and does not return).
LockEvent lockTick();
