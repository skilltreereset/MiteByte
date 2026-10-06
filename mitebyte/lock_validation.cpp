#include "storage.h"
#include "config.h"

bool storageLockSequenceIsValid(const String &seq) {
  if (seq.length() < LOCK_SEQ_MIN_LEN || seq.length() > LOCK_SEQ_MAX_LEN) return false;
  for (size_t i = 0; i < seq.length(); i++) {
    if (seq[i] != 'S' && seq[i] != 'L') return false;
  }
  return true;
}

bool storageLockSequencesAreCompatible(const String &unlock, const String &hardlock) {
  // Matching runs after each press. Either sequence contained in the other
  // can trigger its action before the intended gesture is finished.
  return storageLockSequenceIsValid(unlock) && storageLockSequenceIsValid(hardlock) &&
         unlock.indexOf(hardlock) < 0 && hardlock.indexOf(unlock) < 0;
}

bool storageLongPressIsValid(uint16_t ms) {
  return ms >= LOCK_LONG_PRESS_MIN_MS && ms <= LOCK_LONG_PRESS_MAX_MS;
}
