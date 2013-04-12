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
    STEP_RAMP_TO_SETPOINT = 0,
    STEP_SOAK_AT_TEMPERATURE = 1,
    STEP_JUMP_TO_SETPOINT = 2,
    STEP_WAIT_TO_CROSS = 3,
    STEP_FLAG_BUZZER = 0x40,
    STEP_EEPROM_SWIZZLE = 0x80,
    STEP_INVALID = 0x7F,
    STEP_CONTENT_MASK = 0x7F,
    STEP_TYPE_MASK = 0x3F
  };

  enum { NR_STEPS = 16, NAME_LENGTH = 8 };

  char name[NAME_LENGTH];
  byte stepTypes[NR_STEPS];
  unsigned long stepDurations[NR_STEPS];
  double stepEndpoints[NR_STEPS];

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

