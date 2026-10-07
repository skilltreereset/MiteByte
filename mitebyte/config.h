#pragma once
#include <Arduino.h>

#define FIRMWARE_VERSION "0.5.0-beta.1"

#define AP_SSID_PREFIX     "MiteByte"
#define AP_PASSWORD_PREFIX "flea-"
#define AP_CHANNEL     6
#define AP_MAX_CLIENTS 4
#define MDNS_HOST      "mitebyte"

#define TFT_MOSI 3
#define TFT_SCLK 5
#define TFT_CS   4
#define TFT_DC   2
#define TFT_RST  1
#define TFT_BL   38

// Active low: 0 is full brightness, 255 is off.
#define TFT_BL_DUTY_ON 0

#define BRIGHTNESS_MIN 0
#define BRIGHTNESS_MAX 100
#define SCREEN_BRIGHTNESS_DEFAULT 100
// Maps to the old APA102 global brightness of 6/31.
#define LED_BRIGHTNESS_DEFAULT 20

#define TFT_ROTATION 1

#define TFT_SWAP_RED_BLUE 1

#define LED_DI_PIN 40
#define LED_CI_PIN 39
#define LED_BRIGHTNESS 6

#define SD_MMC_CLK_PIN 12
#define SD_MMC_CMD_PIN 16
#define SD_MMC_D0_PIN  14
#define SD_MMC_D1_PIN  17
#define SD_MMC_D2_PIN  21
#define SD_MMC_D3_PIN  18

#define BOOT_PIN 0

#define DEFAULT_CHAR_DELAY_MS 8
#define DEFAULT_LINE_DELAY_MS 0
#define MAX_SCRIPT_BYTES      16384
#define MAX_LOG_BYTES         4096

// Low-water mark for the run log. Trimming back to the limit itself would
// make every later line reallocate and copy the whole buffer, so it drops
// to here and coasts for a few dozen lines before trimming again.
#define LOG_KEEP_BYTES        3072

#define SCRIPT_DIR   "/payloads"
#define SETTINGS_FILE "/settings.txt"

// Snapshot of the script armed for the next boot. Kept out of SCRIPT_DIR so
// it never shows in the library, and its existence is the armed state: there
// is no second copy of that fact to drift out of sync.
#define ARMED_FILE    "/armed.txt"
#define MAX_NAME_LEN  40

#define SSID_MIN_LEN     1
#define SSID_MAX_LEN     32
#define PASSWORD_MIN_LEN 8
#define PASSWORD_MAX_LEN 63

#define FACTORY_RESET_HOLD_MS 10000

// --- Insertion lock -------------------------------------------------------
// The dongle comes up LOCKED: mass storage only, screen off, with the
// radio, HID and web interface held down until the button is tapped in the
// right order. A press held at or past the configured long-press threshold
// counts as long. The screen stays dark the whole time; presses are counted
// blind, with no on-screen feedback.
//
// Sequences are a string of 'S'/'L' in order, matched against the tail of the
// recent presses, so a mistake slides the window instead of resetting it.
// The values below are only the compiled-in defaults: both sequences and the
// threshold live in Settings and are configurable from the web UI.
#define LOCK_LONG_PRESS_MS      200
#define LOCK_LONG_PRESS_MIN_MS  50
#define LOCK_LONG_PRESS_MAX_MS  5000
#define LOCK_PRESS_DEBOUNCE_MS  30
#define LOCK_MAX_PRESSES        12
#define LOCK_SEQ_MIN_LEN        1
#define LOCK_SEQ_MAX_LEN        12
#define DEFAULT_UNLOCK_SEQUENCE   "SSSLL"
#define DEFAULT_HARDLOCK_SEQUENCE "SSSSS"

// Holding the button 2 s while ONLINE (screen on, device unlocked) enters
// SCREEN_LOCK: the screen and the button are locked down to just the unlock
// gesture, but the radio, HID and web keep running untouched. Exit is the
// same unlock sequence as LOCKED, not a separate one.
#define SCREEN_LOCK_ENTER_HOLD_MS 2000

// A press released between this and the screen-lock hold, while ONLINE, is a
// hold: it opens the script menu, and inside the menu runs the selection.
#define MENU_HOLD_MS 400

// The menu closes itself after this long without a press.
#define MENU_IDLE_MS 4000

// Snapshot of a pending hard-lock, outside settings.txt so a factory reset
// (which deletes settings.txt) also clears it outright. Its presence/count is
// the state: no second copy of that fact to drift out of sync. Armed with a
// count of 1, meaning "one more boot after this reinsertion stays locked", so
// clearing it takes two separate reinsertions, not one.
#define HARDLOCK_FILE "/hardlock.txt"

// How many reinsertions stay locked after the hard-lock gesture is armed.
// Configurable from the web UI; 1 means the next reinsertion stays locked and
// the one after that can unlock (two reinsertions to recover).
#define HARDLOCK_REINSERTS_DEFAULT 1
#define HARDLOCK_REINSERTS_MIN     1
#define HARDLOCK_REINSERTS_MAX     20

// The LED reports state rather than taste, so the colours are fixed.
// Standby blue, red while waiting for a device or running, green once a
// script lands.
#define LED_STANDBY_R 0x00
#define LED_STANDBY_G 0x28
#define LED_STANDBY_B 0xC8
#define LED_DONE_R    0x00
#define LED_DONE_G    0xC8
#define LED_DONE_B    0x3C

#define LED_BREATH_WAIT_MS 2200
#define LED_BREATH_RUN_MS  700
#define LED_OUTCOME_MS     5000

// Colours never cut over. Half of this fades the old one out, half fades the
// new one in, so green to blue dips through black instead of through the
// muddy teals a straight interpolation would cross.
#define LED_FADE_MS  420
#define LED_FRAME_MS 16

#define SCREEN_WAKE_MS 8000

#define START_DELAY_MAX 3600

// Bumping this overwrites the bundled example scripts on next boot.
#define SCRIPT_SEED_VERSION 8

#define DEVICE_NAME_DEFAULT "MiteByte"
#define DEVICE_NAME_MAX     16
