#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/crc16.h>
#include <EEPROM.h>
#include "ospCards.h"
#include "ospSettingsHelper.h"

/*******************************************************************************
* The controller stores all of its settings in the on-chip EEPROM, which is rated
* for 100K erase/program cycles. Settings are saved in several distinct blocks,
* each of which is protected by a CRC-16 validation code. The first block is 200
* bytes long, and contains all of the global controller parameters:
*
* Offset | Size | Item
* --------------------
*      0 |    2 | CRC-16
*      2 |    1 | settings byte
*      3 |    1 | (reserved for future use)
*      4 |   16 | Controller name
*     20 |    4 | P gain
*     24 |    4 | I gain
*     28 |    4 | D gain
*     32 |   16 | 4 setpoints
*     48 |    4 | Autotune step parameter
*     52 |    4 | Autotune noise parameter
*     56 |    4 | Autotune look-back parameter
*     60 |    4 | Manual output setting
*     64 |    8 | (reserved for future use)
*     72 |   64 | Input card setting space
*    136 |   64 | Output card setting space
*
* This block is followed by 3 profile blocks, each 154 B long:
*
* Offset | Size | Item
* --------------------
*      0 |    2 | CRC-16
*      2 |    8 | profile name
*     10 |   16 | Step types (16 x 1 byte each)
*     26 |   64 | Step durations (16 x 4 bytes each)
*     90 |   64 | Step endpoints (16 x 4 bytes each)
*
* The 5th block does not have a CRC-16 of its own, because it is a ring buffer
* for recording the controllers progress through an executing profile. The buffer
* is 32 x 4 = 128 bytes long, consisting of 32 slots for 4-byte execution records:
*
* Offset | Size | Item
* --------------------
*      0 |    2 | CRC-16 of currently executing profile
*      2 |    2 | 2 bytes initialized to 0xFFFF, and each successive bit from the
*                 LSB cleared as that step in the profile is completed
*
* If the status bytes are 0x0000, the profile is no longer executing; we record
* the status in this way because with a little bit of inline assembler we can
* clear bits in the EEPROM without having to erase it -- so that means that the
* entire profile only requires _one_ erase/program cycle per execution, as
* opposed to one per step. This comes out to a EEPROM lifetime of ~3 years if
* a profile is executed every 30 seconds, continuously. We also clear the CRC-16
* field of the _previous_ block to zero when we start a new profile, so that we
* can keep track of the location within the ring buffer. We try to guarantee that
* no profile will ever have 0x0000 as its CRC-16.
*
* If EEPROM lifetime is a concern, it is also possible to turn off the profile
* execution logging by changing the startup mode of the controller.
*******************************************************************************/

enum {
  SETTINGS_CRC_OFFSET = 0,
  SETTINGS_SBYTE_OFFSET = 2,
  SETTINGS_NAME_OFFSET = 4,
  SETTINGS_NAME_LENGTH = 16,
  SETTINGS_P_OFFSET = 20,
  SETTINGS_I_OFFSET = 24,
  SETTINGS_D_OFFSET = 28,
  SETTINGS_SP_OFFSET = 32,
  NR_SETPOINTS = 4,
  SETTINGS_AT_STEP_OFFSET = 48,
  SETTINGS_AT_NOISE_OFFSET = 52,
  SETTINGS_AT_LOOKBACK_OFFSET = 56,
  SETTINGS_OUTPUT_OFFSET = 60,
  INPUT_CARD_SETTINGS_OFFSET = 72,
  OUTPUT_CARD_SETTINGS_OFFSET = 136,
  SETTINGS_CRC_LENGTH = 198
};

enum {
  NR_PROFILES = 3,
  PROFILE_BLOCK_START_OFFSET = 200,
  PROFILE_BLOCK_LENGTH = 154,
  PROFILE_CRC_OFFSET = 0,
  PROFILE_NAME_OFFSET = 2,
  PROFILE_STEP_TYPES_OFFSET = 10,
  PROFILE_STEP_DURATIONS_OFFSET = 26,
  PROFILE_STEP_ENDPOINTS_OFFSET = 90,
  PROFILE_CRC_LENGTH = 152
};

enum {
  STATUS_BUFFER_START_OFFSET = 462,
  STATUS_BUFFER_LENGTH = 128,
  STATUS_BUFFER_BLOCK_LENGTH = 4,
  STATUS_BLOCK_CRC_OFFSET = 0,
  STATUS_BLOCK_STATUS_OFFSET = 2
};

// the start value for doing CRC-16 cyclic redundancy checks
#define CRC16_INIT 0xffff

// the index of the currently in-use (or next free) status buffer block
byte statusBufferIndex = 0;

// check each EEPROM block and either restore it or reset it
void setupEEPROM()
{
  // first check the profiles
  for (byte i = 0; i < NR_PROFILES; i++) {
    if (!checkEEPROMProfile(i)) {
      // bad CRC: clear this profile by writing it using our hardcoded
      // "empty profile" defaults
      saveEEPROMProfile(i);
    }
  }

  // then check and restore the global settings
  if (checkEEPROMSettings()) {
    restoreEEPROMSettings();
  } else {
    // bad CRC: save our hardcoded defaults
    saveEEPROMSettings();
  }
}

// force a reset to factory defaults
void clearEEPROM() {
  // overwrite the CRC-16s
  int zero = 0;
  ospSettingsHelper::eepromWrite(SETTINGS_CRC_OFFSET, zero);

  for (byte i = 0; i < NR_PROFILES; i++) {
    ospSettingsHelper::eepromWrite(PROFILE_BLOCK_START_OFFSET + i*PROFILE_BLOCK_LENGTH, zero);
  }

  // since the status buffer blocks are tagged by CRC-16 values, we don't need to do
  // anything with them
}

int checkEEPROMBlockCrc(int address, int length)
{
  int crc = CRC16_INIT;

  while (length--) {
    byte b = eeprom_read_byte((byte *)address);
    crc = _crc16_update(crc, b);
    address++;
  }

  return crc;
}

// check the CRC-16 of the settings block
bool checkEEPROMSettings()
{
  int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(SETTINGS_SBYTE_OFFSET, SETTINGS_CRC_LENGTH);
  ospSettingsHelper::eepromRead(SETTINGS_CRC_OFFSET, storedCrc);

  return (calculatedCrc == storedCrc);
}

union SettingsByte {
  struct {
    byte pidMode : 1;
    byte pidDirection : 1;
    byte powerOnBehavior : 2;
    byte setpointIndex : 2;
    byte spare : 2;
  };
  byte byteVal;
};

void saveEEPROMSettings()
{
  SettingsByte sb;
  ospSettingsHelper settings(CRC16_INIT, SETTINGS_SBYTE_OFFSET);

  sb.byteVal = 0xFF;
  sb.pidMode = modeIndex;
  sb.pidDirection = ctrlDirection;
  sb.powerOnBehavior = powerOnBehavior;
  sb.setpointIndex = setpointIndex;
  settings.save(sb.byteVal);

  for (byte i = 0; i < SETTINGS_NAME_LENGTH; i++)
    settings.save(controllerName[i]);

  settings.save(kp);
  settings.save(ki);
  settings.save(kd);

  for (byte i = 0; i < NR_SETPOINTS; i++)
    settings.save(setPoints[i]);

  setpoint = setPoints[setpointIndex];

  settings.save(aTuneStep);
  settings.save(aTuneNoise);
  settings.save(aTuneLookBack);

  settings.save(output);

  settings.fillUpTo(INPUT_CARD_SETTINGS_OFFSET);
  theInputCard.saveSettings(settings);
  settings.fillUpTo(OUTPUT_CARD_SETTINGS_OFFSET);
  theOutputCard.saveSettings(settings);
}

void restoreEEPROMSettings()
{
  SettingsByte sb;
  ospSettingsHelper settings(CRC16_INIT, SETTINGS_SBYTE_OFFSET);

  settings.restore(sb.byteVal);
  modeIndex = sb.pidMode;
  ctrlDirection = sb.pidDirection;
  powerOnBehavior = sb.powerOnBehavior;
  setpointIndex = sb.setpointIndex;

  for (byte i = 0; i < SETTINGS_NAME_LENGTH; i++)
    settings.restore(controllerName[i]);

  settings.restore(kp);
  settings.restore(ki);
  settings.restore(kd);

  for (byte i = 0; i < NR_SETPOINTS; i++)
    settings.restore(setPoints[i]);

  setpoint = setPoints[setpointIndex];

  settings.restore(aTuneStep);
  settings.restore(aTuneNoise);
  settings.restore(aTuneLookBack);

  settings.restore(output);

  settings.skipTo(INPUT_CARD_SETTINGS_OFFSET);
  theInputCard.restoreSettings(settings);
  settings.skipTo(OUTPUT_CARD_SETTINGS_OFFSET);
  theOutputCard.restoreSettings(settings);
}

// check the CRC-16 of the i'th profile block
bool checkEEPROMProfile(byte index)
{
  int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(base + PROFILE_CRC_OFFSET, PROFILE_CRC_LENGTH);
  ospSettingsHelper::eepromRead(base + PROFILE_CRC_OFFSET, storedCrc);

  return (calculatedCrc == storedCrc);
}

// write the profileBuffer to the i'th profile block
void saveEEPROMProfile(byte index)
{
  const int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  byte swizzle = 0x80; // we start by or-ing 0x80 into the stepTypes

retry:
  ospSettingsHelper settings(CRC16_INIT, base + 2); // skip the CRC-16 slot

  // write the profile settings and calculate the CRC-16
  for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
    settings.save(profileBuffer.name[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepTypes[i] | swizzle);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepDurations[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepEndpoints[i]);

  int crcValue = settings.crcValue();

  if (crcValue == 0 && swizzle) {
    // zero is a reserved value in the status buffer, so take out the swizzle
    // and retry to generate a different CRC-16
    swizzle = 0x00;
    goto retry;
  }

  // and now write the CRC-16
  ospSettingsHelper::eepromWrite(base, crcValue);
}

bool tryRestoreRingbufferState()
{
  return false;
}

void startEEPROMProfileExecution(byte profileIndex)
{
}

// finish a step: loads the command for the next step from EEPROM and returns
// true, or returns false if that was the last step
bool completeEEPROMProfileStep(byte profileIndex, byte step)
{
  return false;
}

void finishEEPROMProfileExecution(byte profileIndex)
{
}

