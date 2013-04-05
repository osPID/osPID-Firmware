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
*     60 |   12 | (reserved for future use)
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
* a profile is executed every 30 seconds, continuously.
*
* If EEPROM lifetime is a concern, it is also possible to turn off the profile
* execution logging by changing the startup mode of the controller.
*******************************************************************************/

enum {
    SETTINGS_CRC_OFFSET = 0,
    SETTINGS_SBYTE_OFFSET = 1,
    SETTINGS_NAME_OFFSET = 2,
    SETTINGS_NAME_LENGTH = 16,
    SETTINGS_P_OFFSET = 20,
    SETTINGS_I_OFFSET = 24,
    SETTINGS_D_OFFSET = 28,
    SETTINGS_SP_OFFSET = 32,
    NR_SETPOINTS = 4,
    SETTINGS_AT_STEP_OFFSET = 48,
    SETTINGS_AT_NOISE_OFFSET = 52,
    SETTINGS_AT_LOOKBACK_OFFSET = 56,
    INPUT_CARD_SETTINGS_OFFSET = 72,
    OUTPUT_CARD_SETTINGS_OFFSET = 136
};

enum {
    PROFILE_BLOCK_START_OFFSET = 200,
    PROFILE_BLOCK_LENGTH = 154,
    PROFILE_CRC_OFFSET = 0,
    PROFILE_NAME_OFFSET = 2,
    PROFILE_STEP_TYPES_OFFSET = 10,
    PROFILE_STEP_DURATIONS_OFFSET = 26,
    PROFILE_STEP_ENDPOINTS_OFFSET = 90
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

const int eepromTuningOffset = 1; //13 bytes
const int eepromDashOffset = 14; //9 bytes
const int eepromATuneOffset = 23; //12 bytes
const int eepromProfileOffset = 35; //136 bytes
const int eepromInputOffset = 172; //? bytes (depends on the card)
const int eepromOutputOffset = 300; //? bytes (depends on the card)

const byte EEPROM_ID = 2; //used to automatically trigger and eeprom reset after firmware update (if necessary)

void initializeEEPROM()
{
  //read in eeprom values
  byte firstTime = EEPROM.read(0);
  if(firstTime!=EEPROM_ID)
  {//the only time this won't be 1 is the first time the program is run after a reset or firmware update
    //clear the EEPROM and initialize with default values
    for(int i=1;i<1024;i++) EEPROM.write(i,0);
    EEPROMBackupTunings();
    EEPROMBackupDash();
    EEPROMBackupATune();

    ospSettingsHelper settingsHelper(CRC16_INIT, eepromInputOffset);
    theInputCard.saveSettings(settingsHelper);
    settingsHelper.fillUpTo(eepromOutputOffset);
    theOutputCard.saveSettings(settingsHelper);

    EEPROMBackupProfile();
    EEPROM.write(0,EEPROM_ID); //so that first time will never be true again (future firmware updates notwithstanding)
  }
  else
  {
    EEPROMRestoreTunings();
    EEPROMRestoreDash();
    EEPROMRestoreATune();

    ospSettingsHelper settingsHelper(CRC16_INIT, eepromInputOffset);
    theInputCard.restoreSettings(settingsHelper);
    settingsHelper.fillUpTo(eepromOutputOffset);
    theOutputCard.restoreSettings(settingsHelper);

    EEPROMRestoreProfile();    
  }
}  

void clearEEPROM()
{
  EEPROM.write(0, 0);
}

void EEPROMreset()
{
  EEPROM.write(0,0);
}


void EEPROMBackupTunings()
{
  EEPROM.write(eepromTuningOffset,ctrlDirection);
  EEPROM_writeAnything(eepromTuningOffset+1,kp);
  EEPROM_writeAnything(eepromTuningOffset+5,ki);
  EEPROM_writeAnything(eepromTuningOffset+9,kd);
}

void EEPROMRestoreTunings()
{
  ctrlDirection = EEPROM.read(eepromTuningOffset);
  EEPROM_readAnything(eepromTuningOffset+1,kp);
  EEPROM_readAnything(eepromTuningOffset+5,ki);
  EEPROM_readAnything(eepromTuningOffset+9,kd);
}

void EEPROMBackupDash()
{
  EEPROM.write(eepromDashOffset, (byte)myPID.GetMode());
  EEPROM_writeAnything(eepromDashOffset+1,setpoint);
  EEPROM_writeAnything(eepromDashOffset+5,output);
}

void EEPROMRestoreDash()
{
  modeIndex = EEPROM.read(eepromDashOffset);
  EEPROM_readAnything(eepromDashOffset+1,setpoint);
  EEPROM_readAnything(eepromDashOffset+5,output);
}

void EEPROMBackupATune()
{
  EEPROM_writeAnything(eepromATuneOffset,aTuneStep);
  EEPROM_writeAnything(eepromATuneOffset+4,aTuneNoise);
  EEPROM_writeAnything(eepromATuneOffset+8,aTuneLookBack);
}

void EEPROMRestoreATune()
{
  EEPROM_readAnything(eepromATuneOffset,aTuneStep);
  EEPROM_readAnything(eepromATuneOffset+4,aTuneNoise);
  EEPROM_readAnything(eepromATuneOffset+8,aTuneLookBack);
}

void EEPROMBackupProfile()
{
  EEPROM_writeAnything(eepromProfileOffset, profname);
  EEPROM_writeAnything(eepromProfileOffset + 8, proftypes);
  EEPROM_writeAnything(eepromProfileOffset + 24, profvals);
  EEPROM_writeAnything(eepromProfileOffset + 85, proftimes); //there might be a slight issue here (/1000?)
}

void EEPROMRestoreProfile()
{
  EEPROM_readAnything(eepromProfileOffset, profname);
  EEPROM_readAnything(eepromProfileOffset + 8, proftypes);
  EEPROM_readAnything(eepromProfileOffset + 24, profvals);
  EEPROM_readAnything(eepromProfileOffset + 85, proftimes); //there might be a slight issue here (/1000?)
}

