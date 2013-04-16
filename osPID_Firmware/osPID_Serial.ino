/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "ospAssert.h"
#include "ospProfile.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("COMM"), __LINE__);

/* The serial command format for the controller is meant to be human-readable.
It consists of a one-letter command, followed by one or more parameter values
separated by spaces (' '). The command is terminated by a newline '\n'; an optional
carriage-return '\r' before the newline is ignored.

Each command may return some data, and WILL return a response code on a separate
line. (Command responses use '\n' as the newline separator.)

Notation: #Number is a simple floating point number of the form -?NNN(.NNN) where
NNN is any number of decimal digits. #N1-N1 is a small integer of in the range N1
to N2, inclusive. #Integer is an integer. #String is a character string, defined
as all characters received up to the '\n' or '\r\n' which terminates the command.
There is no special quoting: embedded spaces are included in the string.

Command list:
  A -- begin an Auto-tune

  a #Number #Number #Integer -- set Autotune parameters: step, noise, and lookback

  C -- Cancel any profile execution or auto-tune currently in progress

  c #Integer -- set the Comm speed, in kbps

  D #Number -- set D gain

  E #String -- Execute the profile of the given name

  e #0-2 -- Execute the profile of the given number

  I -- Identify -- returns two lines: "osPid vX.YYtag" and "Unit {unitName}"

  i #Number -- set I gain

  K #Integer -- peeK at memory address, +ve number = SRAM, -ve number = EEPROM; returns
  the byte value in hexadecimal

  k #Integer #Integer -- poKe at memory address: the first number is the address,
  the second is the byte to write there, in decimal

  L #Number -- set interLock trip point

  l #0-1 -- enable or disable interlock temperature Limit

  M #0-1 -- set the loop to manual/automatic Mode

  N #String -- Name unit

  n #String -- clear the profile buffer and give it a Name

  O #Number -- set output value

  P #Integer #Integer #Number -- add a steP to the profile buffer with the
  numbers being { type, duration, endpoint }

  p #Number -- set P gain

  Q -- Query -- returns status lines: "S {setpoint}", "I {input}", "O
  {output}" plus "P {profile name} {profile step}" if a profile is active
  or "A active" if currently auto-tuning

  R #0-1 -- diRection -- set PID gain sign

  r #Integer -- Reset the memory to the hardcoded defaults, if and only if the number is -999

  S #Number -- Setpoint -- change the (currently active) setpoint

  s #0-3 -- Select setpoint -- changes which setpoint is active

  V #0-2 -- saVe the profile buffer to profile N

  X -- eXamine: dump the unit's settings

  x #0-2 -- eXamine profile: dump profile N

Response codes:
  OK -- success
  EINV -- invalid command or value
  EMOD -- unit in wrong mode for command (e.g. an attempt to set the gains during an auto-tune)

Programming in a profile is performed as a multi-command process. First the
profile buffer is opened by
  n profNam
where profNam is a <= 7 character name for the profile. Then 0-16 profile steps
are programmed through
  P stepType durationInMilliseconds #Number
and finally the profile is saved to profile buffer #N with
  V #N
. The profile can then be executed by name or number with
  E profNam
or
  e #N
. Profiles _must_ be saved before they can be executed.
*/

enum {
  SERIAL_SPEED_9p6k = 0,
  SERIAL_SPEED_14p4k = 1,
  SERIAL_SPEED_19p2k = 2,
  SERIAL_SPEED_28p8k = 3,
  SERIAL_SPEED_38p4k = 4,
  SERIAL_SPEED_57p6k = 5,
  SERIAL_SPEED_115k = 6
};

byte serialSpeed = SERIAL_SPEED_28p8k;

PROGMEM long serialSpeedTable[7] = { 9600, 14400, 19200, 38400, 57600, 115200 };

void setupSerial()
{
  ospAssert(serialSpeed >= 0 && serialSpeed < 7);

  Serial.end();
  long int kbps = pgm_read_dword_near(&serialSpeedTable[serialSpeed]);
  Serial.begin(kbps);
}

// parse an int out of a string; returns a pointer to the first non-numeric
// character
const char * parseInt(const char *str, long *out)
{
  long value = 0;
  bool isNegative = false;

  if (str[0] == '-')
  {
    isNegative = true;
    str++;
  }

  while (true)
  {
    char c = *str;

    if (c < '0' || c > '9')
    {
      if (isNegative)
        value = -value;

      *out = value;
      return str;
    }

    str++;
    value = value * 10 + (c - '0');
  }
}

// parse a simple floating-point value out of a string; returns a pointer
// to the first non-numeric character
const char * parseFloat(const char *str, double *out)
{
  bool isNegative = false;
  bool hasFraction = false;
  double multiplier = 1.0;
  long value;

  *out = NAN;

  if (str[0] == '-')
  {
    isNegative = true;
    str++;
  }

  while (true)
  {
    char c = *str;

    if (c == '.')
    {
      if (hasFraction)
        goto end_of_number;
      hasFraction = true;
      str++;
      continue;
    }

    if (c < '0' || c > '9')
    {
end_of_number:
      if (isNegative)
        value = -value;

      *out = value * multiplier;
      return str;
    }

    str++;
    value = value * 10 + (c - '0');

    if (hasFraction)
      multiplier *= 0.1;
  }
}

// since the serial buffer is finite, we perform a realtime loop iteration
// between each serial write
template<typename T> void __attribute__((noinline)) serialPrint(T t)
{
  Serial.print(t);
  realtimeLoop();
}

template<typename T> void __attribute__((noinline)) serialPrintln(T t)
{
  Serial.println(t);
  realtimeLoop();
}

bool cmdSetSerialSpeed(long speed)
{
  for (byte i = 0; i < (sizeof(serialSpeedTable) / sizeof(serialSpeedTable[0])); i++)
  {
    long s = pgm_read_dword_near(&serialSpeedTable[i]);
    if (s == speed)
    {
      // this is a speed we can do: report success, and then reset the serial
      // interface to the new speed
      serialSpeed = i;
      Serial.println(F("OK"));
      Serial.flush();

      // we have to report success _first_, because changing the serial speed will
      // break the connection!
      markSettingsDirty();
      setupSerial();
      return true;
    }
  }
  return false;
}

bool cmdStartProfile(const char *name)
{
  for (byte i = 0; i < NR_PROFILES; i++)
  {
    byte ch = 0;
    const char *p = name;
    bool match = true;

    while (*p && ch < ospProfile::NAME_LENGTH)
    {
      if (*p != getProfileNameCharAt(i, ch))
        match = false;
      p++;
      ch++;
    }

    if (match && ch <= ospProfile::NAME_LENGTH)
    {
      // found the requested profile: start it
      activeProfileIndex = i;
      startProfile();
      return true;
    }
  }
  return false;
}

void cmdPeek(int address)
{
  byte val;

  if (address < 0)
    ospSettingsHelper::eepromRead(-address, val);
  else
    val = * (byte *)address;

  Serial.print('0' + (val >> 4));
  Serial.print('0' + (val & 0x0F));
}

void cmdPoke(int address, byte val)
{
  if (address < 0)
    ospSettingsHelper::eepromWrite(-address, val);
  else
    *(byte *)address = val;
}

void cmdIdentify()
{
  Serial.println(F("osPID " OSPID_VERSION_TAG));
  Serial.print(F("Unit "));
  serialPrintln(controllerName);
}

void cmdQuery()
{
}

void cmdExamineSettings()
{
}

void cmdExamineProfile(byte index)
{
}

// this is the entry point to the serial command processor: it is called
// when a '\n' has been received over the serial connection, and therefore
// a full command is buffered in serialCommandBuffer
void processSerialCommand()
{
  const char *p = &serialCommandBuffer[1], *p2;
  double f1, f2;
  long i1, i2;

  if (serialCommandBuffer[--serialCommandLength] != '\n')
    goto out_EINV;

#define CHECK_SPACE()                                   \
  if ((*p++) != ' ')                                    \
    goto out_EINV;                                      \
  else do { } while (0)
#define CHECK_P2()                                      \
  if (p2 == p)                                          \
    goto out_EINV;                                      \
  else do { p = p2; } while (0)
#define CHECK_CMD_END()                                 \
  if (*p != '\n' && !(*p == '\r' && *(++p) == '\n'))    \
    goto out_EINV;                                      \
  else do { } while (0)
#define BOUNDS_CHECK(f, min, max)                       \
  if ((f) < (min) || (f) > (max))                       \
    goto out_EINV;                                      \
  else do { } while (0)

  switch (serialCommandBuffer[0])
  {
  case 'A': // start an auto-tune
    CHECK_CMD_END();
    if (runningProfile || tuning)
      goto out_EMOD;
    startAutoTune();
    goto out_OK;

  case 'a': // set the auto-tune parameters
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseFloat(p, &f2);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();

    aTuneStep = f1;
    aTuneNoise = f2;
    aTuneLookBack = i1;
    markSettingsDirty();
    goto out_OK;

  case 'C': // cancel an auto-tune or profile execution
    CHECK_CMD_END();
    if (tuning)
      stopAutoTune();
    else if (runningProfile)
      stopProfile();
    else
      goto out_EMOD;
    goto out_OK;

  case 'c': // set the comm speed
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();

    if (cmdSetSerialSpeed(i1)) // since this resets the interface, just return
      return;
    goto out_EINV;

  case 'D': // set the D gain
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(f1, 0, 100);

    if (tuning)
      goto out_EMOD;
    kd = f1;
    markSettingsDirty();
    goto out_OK;

  case 'E': // execute a profile by name
    CHECK_SPACE();
    // remove the trailing '\n' or '\r\n' before reading the command name #String
    if (serialCommandBuffer[serialCommandLength - 1] == '\r')
      serialCommandBuffer[--serialCommandLength] = '\0';
    else
      serialCommandBuffer[serialCommandLength] = '\0';

    if (tuning || runningProfile || modeIndex != AUTOMATIC)
      goto out_EMOD;

    if (!cmdStartProfile(p))
      goto out_EINV;
    goto out_OK;

  case 'e': // execute a profile by number
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(i1, 0, NR_PROFILES-1);

    if (tuning || runningProfile || modeIndex != AUTOMATIC)
      goto out_EMOD;

    activeProfileIndex = i1;
    startProfile();
    goto out_OK;

  case 'I': // identify
    CHECK_CMD_END();
    cmdIdentify();
    goto out_OK;

  case 'i': // set the I gain
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(f1, 0, 100);

    if (tuning)
      goto out_EMOD;
    ki = f1;
    markSettingsDirty();
    goto out_OK;

  case 'K': // memory peek
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();

    cmdPeek(i1);
    goto out_OK;

  case 'k': // memory poke
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    p2 = parseInt(p, &i2);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(i2, 0, 255);

    cmdPoke(i1, i2);
    goto out_OK;

  case 'L':
    goto out_EINV;
  case 'l':
    goto out_EINV;

  case 'M': // set the controller mode (PID or manual)
    goto out_EINV;

  case 'N': // set the unit name
    CHECK_SPACE();
    // remove the trailing '\n' or '\r\n' before reading the command name #String
    if (serialCommandBuffer[serialCommandLength - 1] == '\r')
      serialCommandBuffer[--serialCommandLength] = '\0';
    else
      serialCommandBuffer[serialCommandLength] = '\0';

    if (strlen(p) > 16)
      goto out_EINV;

    strcpy(controllerName, p);
    markSettingsDirty();
    goto out_OK;

  case 'n': // clear and name the profile buffer
    CHECK_SPACE();
    // remove the trailing '\n' or '\r\n' before reading the command name #String
    if (serialCommandBuffer[serialCommandLength - 1] == '\r')
      serialCommandBuffer[--serialCommandLength] = '\0';
    else
      serialCommandBuffer[serialCommandLength] = '\0';

    if (strlen(p) > ospProfile::NAME_LENGTH)
      goto out_EINV;

    profileBuffer.clear();
    strcpy(profileBuffer.name, p);
    goto out_OK;

  case 'O': // directly set the output command
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(f1, 0, 100);

    if (tuning || runningProfile || modeIndex != MANUAL)
      goto out_EMOD;

    output = f1;
    goto out_OK;

  case 'P': // define a profile step
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseInt(p, &i2);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();

    if (!profileBuffer.addStep(i1, i2, f1))
      goto out_EINV;
    goto out_OK;

  case 'p': // set the P gain
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(f1, 0, 99.99);

    if (tuning)
      goto out_EMOD;
    kp = f1;
    markSettingsDirty();
    goto out_OK;

  case 'Q': // query current status
    CHECK_CMD_END();
    cmdQuery();
    goto out_OK;

  case 'R': // set the controller action direction
    goto out_EINV;

  case 'r': // reset memory
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();

    if (i1 != -999)
      goto out_EINV;

    clearEEPROM();
    Serial.println(F("Memory marked for reset."));
    Serial.println(F("Reset the unit to complete."));
    goto out_OK;

  case 'S': // change the setpoint
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(f1, 0, 99.99);

    if (tuning)
      goto out_EMOD;

    setPoints[setpointIndex] = f1;
    setpoint = f1;
    markSettingsDirty();
    goto out_OK;

  case 's': // change the active setpoint
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(i1, 0, 3);

    setpointIndex = i1;
    markSettingsDirty();
    goto out_OK;

  case 'V': // save the profile buffer to EEPROM
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(i1, 0, 2);

    saveEEPROMProfile(i1);
    goto out_OK;

  case 'X': // examine: dump the controller settings
    CHECK_CMD_END();
    cmdExamineSettings();
    goto out_OK;

  case 'x': // examine a profile: dump a description of the give profile
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    CHECK_CMD_END();
    BOUNDS_CHECK(i1, 0, 2);

    cmdExamineProfile(i1);
    goto out_OK;

  default:
    goto out_EINV;
  }

#undef CHECK_CMD_END()

out_EINV:
  Serial.println(F("EINV"));
  return;

out_EMOD:
  Serial.println(F("EMOD"));
  return;

out_OK:
  Serial.println(F("OK"));
  return;
}

