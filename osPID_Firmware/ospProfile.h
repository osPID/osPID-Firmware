#ifndef OSPPROFILE_H
#define OSPPROFILE_H

#include <Arduino.h>

// an ospProfile encapsulates the information for a setpoint profile
class ospProfile {
public:
  // note that only (stepTypes[i] & 0x7F) is significant: we reserve one bit
  // which can be freely toggled to make sure that we never get a CRC-16 of
  // 0x0000 when the profile is saved
  enum {
    STEP_RAMP = 0,
    STEP_HOLD = 1,
    STEP_JUMP = 2,
    STEP_BUZZ = 3,
    STEP_WAIT_TO_CROSS = 4,
    STEP_INVALID = 0x7F
  };

  enum { NR_STEPS = 16, NAME_LENGTH = 8 };

  char name[NAME_LENGTH];
  byte stepTypes[NR_STEPS];
  unsigned long stepDurations[NR_STEPS];
  float stepEndpoints[NR_STEPS];

  ospProfile() {
    clear();
  }

  void clear() {
    for (byte i = 0; i < NAME_LENGTH; i++)
      name[i] = "Profil1"[i];
    for (byte i = 0; i < NR_STEPS; i++) {
      stepTypes[i] = STEP_INVALID;
      stepDurations[i] = 0;
      stepEndpoints[i] = 0.0f;
    }
  }
};

#endif

