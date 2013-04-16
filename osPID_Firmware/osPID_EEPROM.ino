/* This file contains all of the nonvolatile state management for the controller */

#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/crc16.h>
#include <EEPROM.h>
#include "ospCards.h"
#include "ospSettingsHelper.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("EEPR"), __LINE__);

/*******************************************************************************
* The controller stores all of its settings in the on-chip EEPROM, which is rated
* for 100K erase/program cycles. Settings are saved in several distinct blocks,
* each of which is protected by a CRC-16 validation code. The first block is 200
* bytes long, and contains all of the global controller parameters:
*
* Offset | Size | Item
* --------------------
*      0 |    2 | CRC-16
*      2 |    1 | settings byte 1
*      3 |    1 | settings byte 2
*      4 |   16 | Controller name
*     20 |    4 | P gain
*     24 |    4 | I gain
*     28 |    4 | D gain
*     32 |   16 | 4 setpoints
*     48 |    4 | Autotune step parameter
*     52 |    4 | Autotune noise parameter
*     56 |    4 | Autotune look-back parameter
*     60 |    4 | Manual output setting
*     64 |    4 | Lower trip limit
*     68 |    4 | Upper trip limit
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
  SETTINGS_SBYTE1_OFFSET = 2,
  SETTINGS_SBYTE2_OFFSET = 3,
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
  STATUS_BLOCK_STATUS_OFFSET = 2,
  STATUS_BLOCK_COUNT = (STATUS_BUFFER_LENGTH / STATUS_BUFFER_BLOCK_LENGTH)
};

// the start value for doing CRC-16 cyclic redundancy checks
#define CRC16_INIT 0xffff

// the index of the currently in-use (or next free) status buffer block
byte statusBufferIndex = 0;

// check each EEPROM block and either restore it or mark it to be reset
static void setupEEPROM()
{
  // first check the profiles
  for (byte i = 0; i < NR_PROFILES; i++) {
    if (!checkEEPROMProfile(i)) {
      // bad CRC: clear this profile by writing it using our hardcoded
      // "empty profile" defaults
      profileBuffer.name[6] = '1' + i;
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
static void clearEEPROM() {
  // overwrite the CRC-16s
  int zero = 0;
  ospSettingsHelper::eepromWrite(SETTINGS_CRC_OFFSET, zero);

  for (byte i = 0; i < NR_PROFILES; i++) {
    ospSettingsHelper::eepromWrite(PROFILE_BLOCK_START_OFFSET + i*PROFILE_BLOCK_LENGTH, zero);
  }

  // since the status buffer blocks are tagged by CRC-16 values, we don't need to do
  // anything with them
}

static int checkEEPROMBlockCrc(int address, int length)
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
static bool checkEEPROMSettings()
{
  int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(SETTINGS_SBYTE1_OFFSET, SETTINGS_CRC_LENGTH);
  ospSettingsHelper::eepromRead(SETTINGS_CRC_OFFSET, storedCrc);

  return (calculatedCrc == storedCrc);
}

union SettingsByte1 {
  struct {
    byte pidMode : 1;
    byte pidDirection : 1;
    byte powerOnBehavior : 2;
    byte setpointIndex : 2;
    byte tripLimitsEnabled : 1;
    byte tripAutoReset : 1;
  };
  byte byteVal;
};

extern byte serialSpeed;

union SettingsByte2 {
  struct {
    byte serialSpeed : 3;
    byte spare : 5;
  };
  byte byteVal;
};

static void saveEEPROMSettings()
{
  SettingsByte1 sb1;
  SettingsByte2 sb2;
  ospSettingsHelper settings(CRC16_INIT, SETTINGS_SBYTE1_OFFSET);

  sb1.byteVal = 0;
  sb1.pidMode = modeIndex;
  sb1.pidDirection = ctrlDirection;
  sb1.powerOnBehavior = powerOnBehavior;
  sb1.setpointIndex = setpointIndex;
  sb1.tripLimitsEnabled = tripLimitsEnabled;
  sb1.tripAutoReset = tripAutoReset;
  settings.save(sb1.byteVal);

  sb2.byteVal = 0;
  sb2.serialSpeed = serialSpeed;
  settings.save(sb2.byteVal);

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

  settings.save(lowerTripLimit);
  settings.save(upperTripLimit);

  settings.fillUpTo(INPUT_CARD_SETTINGS_OFFSET);
  theInputCard.saveSettings(settings);
  settings.fillUpTo(OUTPUT_CARD_SETTINGS_OFFSET);
  theOutputCard.saveSettings(settings);
}

static void restoreEEPROMSettings()
{
  SettingsByte1 sb1;
  SettingsByte2 sb2;
  ospSettingsHelper settings(CRC16_INIT, SETTINGS_SBYTE1_OFFSET);

  settings.restore(sb1.byteVal);
  modeIndex = sb1.pidMode;
  ctrlDirection = sb1.pidDirection;
  powerOnBehavior = sb1.powerOnBehavior;
  setpointIndex = sb1.setpointIndex;
  tripLimitsEnabled = sb1.tripLimitsEnabled;
  tripAutoReset = sb1.tripAutoReset;

  settings.restore(sb2.byteVal);
  serialSpeed = sb2.serialSpeed;

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

  settings.restore(lowerTripLimit);
  settings.restore(upperTripLimit);

  settings.skipTo(INPUT_CARD_SETTINGS_OFFSET);
  theInputCard.restoreSettings(settings);
  settings.skipTo(OUTPUT_CARD_SETTINGS_OFFSET);
  theOutputCard.restoreSettings(settings);
}

// check the CRC-16 of the i'th profile block
static bool checkEEPROMProfile(byte index)
{
  int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(base + PROFILE_CRC_OFFSET, PROFILE_CRC_LENGTH);
  ospSettingsHelper::eepromRead(base + PROFILE_CRC_OFFSET, storedCrc);

  return (calculatedCrc == storedCrc);
}

// write the profileBuffer to the i'th profile block
static void saveEEPROMProfile(byte index)
{
  const int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  byte swizzle = ospProfile::STEP_EEPROM_SWIZZLE; // we start by or-ing 0x80 into the stepTypes

retry:
  ospSettingsHelper settings(CRC16_INIT, base + 2); // skip the CRC-16 slot

  // write the profile settings and calculate the CRC-16
  for (byte i = 0; i < ospProfile::NAME_LENGTH+1; i++)
    settings.save(profileBuffer.name[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepTypes[i] | swizzle);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepDurations[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepEndpoints[i]);

  int crcValue = settings.crcValue();

  if (crcValue == -1 && swizzle) {
    // 0xFFFF is a reserved values in the status buffer, so take out the swizzle
    // and retry the save to generate a different CRC-16
    swizzle = 0x00;
    goto retry;
  }

  // and now write the CRC-16
  ospSettingsHelper::eepromWrite(base, crcValue);
}

static char getProfileNameCharAt(byte profileIndex, byte i)
{
  const int address = PROFILE_BLOCK_START_OFFSET
                        + profileIndex * PROFILE_BLOCK_LENGTH
                        + PROFILE_NAME_OFFSET
                        + i;
  char ch;
  ospAssert(profileIndex >= 0 && profileIndex < NR_PROFILES);
  ospAssert(i >= 0 && i < ospProfile::NAME_LENGTH+1);
  ospSettingsHelper::eepromRead(address, ch);

  return ch;
}

static void getProfileStepData(byte profileIndex, byte i, byte *type, unsigned long *duration, double *endpoint)
{
  const int base = PROFILE_BLOCK_START_OFFSET
                    + profileIndex * PROFILE_BLOCK_LENGTH;


  ospAssert(profileIndex >= 0 && profileIndex < NR_PROFILES);
  ospSettingsHelper::eepromRead(base + PROFILE_STEP_TYPES_OFFSET + i, *type);
  *type &= ospProfile::STEP_CONTENT_MASK;

  ospSettingsHelper::eepromRead(base + PROFILE_STEP_DURATIONS_OFFSET + i*sizeof(unsigned long), *duration);
  ospSettingsHelper::eepromRead(base + PROFILE_STEP_ENDPOINTS_OFFSET + i*sizeof(double), *endpoint);
}

byte currentStatusBufferBlockIndex;

// search through the profiles for one with the given CRC-16, and return
// its index or 0xFF if not found
static byte profileIndexForCrc(int crc)
{
  int profileCrc;

  for (byte i = 0; i < NR_PROFILES; i++)
  {
    int crcAddress = PROFILE_BLOCK_START_OFFSET + i * PROFILE_BLOCK_LENGTH + PROFILE_CRC_OFFSET;
    ospSettingsHelper::eepromRead(crcAddress, profileCrc);

    if (crc == profileCrc)
      return i;
  }
  return 0xFF;
}

// this function reads the status buffer to see if execution of a profile
// was interrupted; if it was, it loads activeProfile and currentProfileStep
// with the step that was interrupted and returns true. If any profile has been
// run, activeProfile is restored to the last profile to have been run.
static bool profileWasInterrupted()
{
  int crc, statusBits;

  for (int blockAddress = STATUS_BUFFER_START_OFFSET;
    blockAddress < STATUS_BUFFER_START_OFFSET + STATUS_BUFFER_LENGTH;
    blockAddress += STATUS_BUFFER_BLOCK_LENGTH, currentStatusBufferBlockIndex++)
  {
    ospSettingsHelper::eepromRead(blockAddress + STATUS_BLOCK_CRC_OFFSET, crc);

    if (crc == -1) // not a valid profile CRC-16
      continue;

    byte profileIndex = profileIndexForCrc(crc);
    if (profileIndex == 0xFF)
      return false; // the recorded profile no longer exists

    // this is either the last profile fully executed or one that was interrupted
    activeProfileIndex = profileIndex;
    ospSettingsHelper::eepromRead(blockAddress + STATUS_BLOCK_STATUS_OFFSET, statusBits);
    if (statusBits != 0)
    {
      // we found an active profile
      currentProfileStep = ffs(statusBits) - 1;
      return true;
    }
    else
      return false;
  }

  // didn't find anything
  currentStatusBufferBlockIndex = 0;
  return false;
}

static void recordProfileStart()
{
  // figure out which status buffer slot to use, and which one was last used
  byte priorBlockIndex = currentStatusBufferBlockIndex;
  currentStatusBufferBlockIndex = (currentStatusBufferBlockIndex + 1) % STATUS_BLOCK_COUNT;

  // record the start of profile execution
  int crc, ffff = 0xFFFF;
  int crcAddress = PROFILE_BLOCK_START_OFFSET + activeProfileIndex * PROFILE_BLOCK_LENGTH + PROFILE_CRC_OFFSET;
  int statusBlockAddress = STATUS_BUFFER_START_OFFSET + currentStatusBufferBlockIndex * STATUS_BUFFER_BLOCK_LENGTH;

  // we only clear bits (no erase) to write the CRC; it will get erased to 0xFFFF when the _next_
  // profile is executed
  ospSettingsHelper::eepromRead(crcAddress, crc);
  ospSettingsHelper::eepromClearBits(statusBlockAddress + STATUS_BLOCK_CRC_OFFSET, crc);
  ospSettingsHelper::eepromWrite(statusBlockAddress + STATUS_BLOCK_STATUS_OFFSET, ffff);

  // and delete the previous execution record
  ospSettingsHelper::eepromWrite(STATUS_BUFFER_START_OFFSET + priorBlockIndex * STATUS_BUFFER_BLOCK_LENGTH, ffff);
}

// clear the Incomplete bit for the just-completed profile step
static void recordProfileStepCompletion(byte step)
{
  int statusAddress = STATUS_BUFFER_START_OFFSET
        + currentStatusBufferBlockIndex * STATUS_BUFFER_BLOCK_LENGTH
        + STATUS_BLOCK_STATUS_OFFSET;

  int status = 0xFFFF;
  status <<= (step + 1);
  ospSettingsHelper::eepromClearBits(statusAddress, status);
}

// mark the profile as complete by clearing all the Incomplete bits, regardless
// of how many steps it actually contained
static void recordProfileCompletion()
{
  recordProfileStepCompletion(15);
}

