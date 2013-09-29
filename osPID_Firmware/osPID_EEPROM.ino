/* This file contains all of the nonvolatile state management for the controller */

#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/crc16.h>
#include <EEPROM.h>
#include "defines.h"
#include "ospIODevice.h"
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
*     20 |    2 | P gain
*     22 |    2 | I gain
*     24 |    2 | D gain
*     26 |    8 | 4 setpoints
*     34 |    2 | Autotune step parameter
*     36 |    2 | Autotune noise parameter
*     38 |    2 | Autotune look-back parameter
*     40 |    2 | Manual output setting
*     42 |    2 | Lower trip limit
*     44 |    2 | Upper trip limit
*     46 |    1 | EEPROM version identifier
*     47 |   25 | (free)
*     72 |   64 | Input device setting space
*    136 |   64 | Output device setting space
*
* This block is followed by 3 profile blocks, each 122 B long:
*
* Offset | Size | Item
* --------------------
*      0 |    2 | CRC-16
*      2 |    8 | profile name
*     10 |   16 | Step types (16 x 1 byte each)
*     26 |   64 | Step durations (16 x 4 bytes each)
*     90 |   32 | Step endpoints (16 x 2 bytes each)
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

// UPDATE THIS VALUE EVERY TIME THE EEPROM LAYOUT CHANGES IN AN INCOMPATIBLE WAY
enum { EEPROM_STORAGE_VERSION = 1 };

enum 
{
  SETTINGS_CRC_OFFSET = 0,
  SETTINGS_SBYTE1_OFFSET = 2,
  SETTINGS_SBYTE2_OFFSET = 3,
  SETTINGS_NAME_OFFSET = 4,
  SETTINGS_NAME_LENGTH = 16,
  SETTINGS_P_OFFSET = 20,
  SETTINGS_I_OFFSET = 22,
  SETTINGS_D_OFFSET = 24,
  SETTINGS_SP_OFFSET = 26,
  NR_SETPOINTS = 4,
  SETTINGS_AT_STEP_OFFSET = 34,
  SETTINGS_AT_NOISE_OFFSET = 36,
  SETTINGS_AT_LOOKBACK_OFFSET = 38,
  SETTINGS_OUTPUT_OFFSET = 40,
  SETTINGS_LOWER_TRIP_OFFSET = 42,
  SETTINGS_UPPER_TRIP_OFFSET = 44,
  SETTINGS_INPUT_TYPE = 46,
  SETTINGS_OUTPUT_TYPE = 47, 
  SETTINGS_DISPLAY_CELSIUS = 48,
  SETTINGS_VERSION_OFFSET = 49,
  // free space from 50 to 71
  INPUT_DEVICE_SETTINGS_OFFSET = 72,
  OUTPUT_DEVICE_SETTINGS_OFFSET = 168,
  SETTINGS_CRC_LENGTH = 198
};

enum 
{
  NR_PROFILES = 3,
  PROFILE_BLOCK_START_OFFSET = 200,
  PROFILE_BLOCK_LENGTH = 130,
  PROFILE_CRC_OFFSET = 0,
  PROFILE_NAME_OFFSET = 2,
  PROFILE_STEP_TYPES_OFFSET = 18,
  PROFILE_STEP_DURATIONS_OFFSET = 34,
  PROFILE_STEP_ENDPOINTS_OFFSET = 98,
  PROFILE_CRC_LENGTH = 128
};

enum 
{
  STATUS_BUFFER_START_OFFSET = PROFILE_BLOCK_START_OFFSET + NR_PROFILES * PROFILE_BLOCK_LENGTH,
  STATUS_BUFFER_LENGTH = 128,
  STATUS_BUFFER_BLOCK_LENGTH = 4,
  STATUS_BLOCK_CRC_OFFSET = 0,
  STATUS_BLOCK_STATUS_OFFSET = 2,
  STATUS_BLOCK_COUNT = (STATUS_BUFFER_LENGTH / STATUS_BUFFER_BLOCK_LENGTH)
};

// the start value for doing CRC-16 cyclic redundancy checks
#define CRC16_INIT 0xFFFFu

// the index of the currently in-use (or next free) status buffer block
byte statusBufferIndex = 0;

// check each EEPROM block and either restore it or mark it to be reset
static void setupEEPROM()
{
  // first check the profiles
  for (byte i = 0; i < NR_PROFILES; i++) 
  {
    if (!checkEEPROMProfile(i)) 
    {
      // bad CRC: clear this profile by writing it using our hardcoded
      // "empty profile" defaults
      drawBadCsum(i);
      //profileBuffer.name[6] = '1' + i;
      saveEEPROMProfile(i);
    }
  }

  // then check and restore the global settings
  if (checkEEPROMSettings()) 
  {
    restoreEEPROMSettings();
  } else 
  {
    // bad CRC: save our hardcoded defaults
    drawBadCsum(0xFF);
    saveEEPROMSettings();
  }
}

// force a reset to factory defaults
static void clearEEPROM() 
{
  // overwrite the CRC-16s
  unsigned int zero = 0;
  ospSettingsHelper::eepromWrite(SETTINGS_CRC_OFFSET, zero);

  for (byte i = 0; i < NR_PROFILES; i++) 
  {
    ospSettingsHelper::eepromWrite(PROFILE_BLOCK_START_OFFSET + i*PROFILE_BLOCK_LENGTH + PROFILE_CRC_OFFSET, zero);
  }

  // since the status buffer blocks are tagged by CRC-16 values, we don't need to do
  // anything with them
}

static unsigned int checkEEPROMBlockCrc(int address, int length)
{
  unsigned int crc = CRC16_INIT;

  while (length--) 
  {
    byte b = eeprom_read_byte((byte *)address);
    crc = _crc16_update(crc, b);
    address++;
  }

  return crc;
}

// check the CRC-16 of the settings block
static bool checkEEPROMSettings()
{
  unsigned int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(SETTINGS_SBYTE1_OFFSET, SETTINGS_CRC_LENGTH);
  ospSettingsHelper::eepromRead(SETTINGS_CRC_OFFSET, storedCrc);

  if (calculatedCrc != storedCrc)
    return false;

  byte storedVersion;
  ospSettingsHelper::eepromRead(SETTINGS_VERSION_OFFSET, storedVersion);
  return (storedVersion == EEPROM_STORAGE_VERSION);
}

union SettingsByte1 
{
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

union SettingsByte2 
{
  struct 
  {
    byte serialSpeed : 3;
    byte activeProfileIndex : 2;
    byte spare : 3;
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
  sb2.activeProfileIndex = activeProfileIndex;
  settings.save(sb2.byteVal);

  for (byte i = 0; i < SETTINGS_NAME_LENGTH; i++)
    settings.save(controllerName[i]);

  settings.save(PGain);
  settings.save(IGain);
  settings.save(DGain);

  for (byte i = 0; i < NR_SETPOINTS; i++)
    settings.save(setPoints[i]);

  settings.save(aTuneStep);
  settings.save(aTuneNoise);
  settings.save(aTuneLookBack);

  settings.save(manualOutput);

  settings.save(lowerTripLimit);
  settings.save(upperTripLimit);
  
  settings.save(inputType);
  settings.save(outputType);

  settings.save(displayCelsius);
  
  settings.save((byte) EEPROM_STORAGE_VERSION);

  settings.fillUpTo(INPUT_DEVICE_SETTINGS_OFFSET);
#ifndef USE_SIMULATOR
  for (byte i = 0; i < 3; i++ )
  {
    inputDevice[i]->saveSettings(settings);
  }
#else
  inputDevice[0]->saveSettings(settings);
#endif

  settings.fillUpTo(OUTPUT_DEVICE_SETTINGS_OFFSET);
  outputDevice[0]->saveSettings(settings);

  // fill any trailing unused space
  settings.fillUpTo(SETTINGS_SBYTE1_OFFSET + SETTINGS_CRC_LENGTH);

  // and finalize the save by writing the CRC
  ospSettingsHelper::eepromWrite(SETTINGS_CRC_OFFSET, settings.crcValue());
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
  activeProfileIndex = sb2.activeProfileIndex;

  for (byte i = 0; i < SETTINGS_NAME_LENGTH; i++)
    settings.restore(controllerName[i]);

  settings.restore(PGain);
  settings.restore(IGain);
  settings.restore(DGain);

  for (byte i = 0; i < NR_SETPOINTS; i++)
    settings.restore(setPoints[i]);

  activeSetPoint = double(setPoints[setpointIndex]);
  displaySetpoint = setPoints[0];

  settings.restore(aTuneStep);
  settings.restore(aTuneNoise);
  settings.restore(aTuneLookBack);

  settings.restore(manualOutput);
  output = double(manualOutput);

  settings.restore(lowerTripLimit);
  settings.restore(upperTripLimit);
  
  settings.restore(inputType);
  settings.restore(outputType);
  
  settings.restore(displayCelsius);

  settings.skipTo(INPUT_DEVICE_SETTINGS_OFFSET);
#ifndef USE_SIMULATOR
  for (byte i = 0; i < 3; i++ )
  {
    inputDevice[i]->restoreSettings(settings);
  }
#else  
  inputDevice[0]->restoreSettings(settings);
#endif
  theInputDevice = inputDevice[inputType];
  displayCalibration = makeDecimal<1>(theInputDevice->getCalibration() * (displayCelsius ? 1.0 : 1.8));

  settings.skipTo(OUTPUT_DEVICE_SETTINGS_OFFSET);
  outputDevice[0]->restoreSettings(settings);
  theOutputDevice = outputDevice[outputType];
  displayWindow = makeDecimal<1>(theOutputDevice->getOutputWindowSeconds());
}

// check the CRC-16 of the i'th profile block
static bool checkEEPROMProfile(byte index)
{
  int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  unsigned int storedCrc, calculatedCrc;

  calculatedCrc = checkEEPROMBlockCrc(base + PROFILE_NAME_OFFSET, PROFILE_CRC_LENGTH);
  ospSettingsHelper::eepromRead(base + PROFILE_CRC_OFFSET, storedCrc);

  return (calculatedCrc == storedCrc);
}

// write the profileBuffer to the i'th profile block
static void saveEEPROMProfile(byte index)
{
  const int base = PROFILE_BLOCK_START_OFFSET + index * PROFILE_BLOCK_LENGTH;
  byte swizzle = ospProfile::STEP_EEPROM_SWIZZLE; // we start by or-ing 0x80 into the stepTypes

retry:
  ospSettingsHelper settings(CRC16_INIT, base + PROFILE_NAME_OFFSET); // skip the CRC-16 slot

  // write the profile settings and calculate the CRC-16
  for (byte i = 0; i < ospProfile::NAME_LENGTH+1; i++)
    settings.save(profileBuffer.name[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(byte(profileBuffer.stepTypes[i] | swizzle));

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepDurations[i]);

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
    settings.save(profileBuffer.stepEndpoints[i]);

  unsigned int crcValue = settings.crcValue();

  if (crcValue == 0xFFFFu && swizzle) {
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
  ospAssert((profileIndex >= 0) && (profileIndex < NR_PROFILES));
  ospAssert((i >= 0) && (i < ospProfile::NAME_LENGTH+1));
  ospSettingsHelper::eepromRead(address, ch);

  return ch;
}

static void getProfileStepData(byte profileIndex, byte i, byte *type, unsigned long *duration, ospDecimalValue<1> *endpoint)
{
  const int base = PROFILE_BLOCK_START_OFFSET
                    + profileIndex * PROFILE_BLOCK_LENGTH;


  ospAssert(profileIndex >= 0 && profileIndex < NR_PROFILES);
  ospSettingsHelper::eepromRead(base + PROFILE_STEP_TYPES_OFFSET + i, *type);
  *type &= ospProfile::STEP_CONTENT_MASK;

  ospSettingsHelper::eepromRead(base + PROFILE_STEP_DURATIONS_OFFSET + i*sizeof(unsigned long), *duration);
  ospSettingsHelper::eepromRead(base + PROFILE_STEP_ENDPOINTS_OFFSET + i*sizeof(ospDecimalValue<1>), *endpoint);
}

static unsigned int getProfileCrc(byte profileIndex)
{
  const int crcAddress = PROFILE_BLOCK_START_OFFSET + profileIndex * PROFILE_BLOCK_LENGTH + PROFILE_CRC_OFFSET;

  unsigned int crc;
  ospSettingsHelper::eepromRead(crcAddress, crc);
  return crc;
}

byte currentStatusBufferBlockIndex;

// search through the profiles for one with the given CRC-16, and return
// its index or 0xFF if not found
static byte profileIndexForCrc(unsigned int crc)
{
  unsigned int profileCrc;

  for (byte i = 0; i < NR_PROFILES; i++)
  {
    profileCrc = getProfileCrc(i);

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
  unsigned int statusBits, crc;

  for (int blockAddress = STATUS_BUFFER_START_OFFSET;
    blockAddress < STATUS_BUFFER_START_OFFSET + STATUS_BUFFER_LENGTH;
    blockAddress += STATUS_BUFFER_BLOCK_LENGTH, currentStatusBufferBlockIndex++)
  {
    ospSettingsHelper::eepromRead(blockAddress + STATUS_BLOCK_CRC_OFFSET, crc);

    if (crc == 0xFFFFu) // not a valid profile CRC-16
      continue;

    byte profileIndex = profileIndexForCrc(crc);
    if (profileIndex == 0xFF)
    {
      // the recorded profile no longer exists: erase it
      ospSettingsHelper::eepromWrite(blockAddress + STATUS_BLOCK_CRC_OFFSET, 0xFFFF);
      continue;
    }

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
  unsigned int crc, ffff = 0xFFFFu;
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

  unsigned int status = 0xFFFFu;
  status <<= (step + 1);
  ospSettingsHelper::eepromClearBits(statusAddress, status);
}

// mark the profile as complete by clearing all the Incomplete bits, regardless
// of how many steps it actually contained
static void recordProfileCompletion()
{
  recordProfileStepCompletion(15);
}

