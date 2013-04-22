/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "ospAssert.h"
#include "ospProfile.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("COMM"), __LINE__);

/* The serial command format for the controller is meant to be human-readable.
It consists of a one-letter command mnemonic, followed by one or more parameter values
separated by spaces (' '). The command is terminated by a newline '\n'; an optional
carriage-return '\r' before the newline is ignored.

Each command may return some data, and WILL return a response code on a separate
line. (Command responses use '\n' as the newline separator.)

Notation: #Number is a simple floating point number of the form -?NNN(.NNN) where
NNN is any number of decimal digits. #N1-N1 is a small integer of in the range N1
to N2, inclusive. #Integer is an integer. #String is a character string, defined
as all characters received up to the '\n' or '\r\n' which terminates the command.
There is no special quoting: embedded spaces are included in the string.

Commands marked with a '?' also have a query form, consisting of the mnemonic,
followed by a question mark '?' and a newline. These commands return the value
or values in the same order as they would be written in the "set" version of the command.
Multiple values are returned one to a line.

Command list:
  A -- begin an Auto-tune

  a? #Number #Number #Integer -- set Autotune parameters: step, noise, and lookback

  B? #Integer #Integer #Number -- set peripheral card floating-point caliBration data:
  the first integer is 0 for the input card and 1 for the output card, the second
  is the parameter index, and the number is the value

  b? #Integer #Integer #Integer -- set peripheral card integer caliBration data:
  the first integer is 0 for the input card and 1 for the output card, the second
  is the parameter index, and the third is the value

  C -- Cancel any profile execution or auto-tune currently in progress

  c? #Integer -- set the Comm speed, in kbps

  D? #Number -- set D gain

  E #String -- Execute the profile of the given name

  e #0-2 -- Execute the profile of the given number

  I -- Identify -- returns two lines: "osPid vX.YYtag" and "Unit {unitName}"

  i? #Number -- set I gain

  K #Integer -- peeK at memory address, +ve number = SRAM, -ve number = EEPROM; returns
  the byte value in hexadecimal

  k #Integer #Integer -- poKe at memory address: the first number is the address,
  the second is the byte to write there, in decimal

  L? #Number #Number -- set interLock lower and upper trip points

  l? #0-1 -- enable or disable interlock temperature Limit

  M? #0-1 -- set the loop to manual/automatic Mode

  N? #String -- Name unit

  n #String -- clear the profile buffer and give it a Name

  O? #Number -- set Output value

  o? #Integer -- set power-On behavior

  P #Integer #Integer #Number -- add a steP to the profile buffer with the
  numbers being { type, duration, endpoint }

  p? #Number -- set P gain

  Q -- Query -- returns status lines: "S {setpoint}", "I {input}", "O
  {output}" plus "P {profile name} {profile step}" if a profile is active
  or "A active" if currently auto-tuning

  R? #0-1 -- diRection -- set PID gain sign

  r #Integer -- Reset the memory to the hardcoded defaults, if and only if the number is -999

  S? #Number -- Setpoint -- change the (currently active) setpoint

  s? #0-3 -- Select setpoint -- changes which setpoint is active

  T? -- query Trip state or clear a trip

  t? #0-1 -- trip auto-reseT -- enable or disable automatic recovery from trips

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

static void setupSerial()
{
  ospAssert(serialSpeed >= 0 && serialSpeed < 7);

  Serial.end();
  long int kbps = pgm_read_dword_near(&serialSpeedTable[serialSpeed]);
  Serial.begin(kbps);
}

// parse an int out of a string; returns a pointer to the first non-numeric
// character
static const char * parseInt(const char *str, long *out)
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
static const char * parseFloat(const char *str, double *out)
{
  bool isNegative = false;
  bool hasFraction = false;
  double multiplier = 1.0;
  long value = 0;

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

static bool cmdSetSerialSpeed(long speed)
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

static bool cmdStartProfile(const char *name)
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

static void cmdPeek(int address)
{
  byte val;

  if (address < 0)
    ospSettingsHelper::eepromRead(-address, val);
  else
    val = * (byte *)address;

  byte b = val >> 4;
  if (b < 10)
    Serial.print('0' + b);
  else
    Serial.print('A' - 10 + b);
  b = val & 0x0F;
  if (b < 10)
    Serial.print('0' + b);
  else
    Serial.print('A' - 10 + b);
}

static void cmdPoke(int address, byte val)
{
  if (address < 0)
    ospSettingsHelper::eepromWrite(-address, val);
  else
    *(byte *)address = val;
}

static void cmdIdentify()
{
  serialPrintln(F("osPID " OSPID_VERSION_TAG));
  serialPrint(F("Unit \""));
  serialPrint(controllerName);
  Serial.println('"');
}

static void cmdQuery()
{
  Serial.print(F("S "));
  serialPrintln(setpoint);
  Serial.print(F("I "));
  serialPrintln(input);
  Serial.print(F("O "));
  serialPrintln(output);

  if (runningProfile)
  {
    Serial.print(F("P \""));
    for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
    {
      char ch = getProfileNameCharAt(activeProfileIndex, i);

      if (ch == '\0')
        break;
      Serial.print(ch);
    }
    Serial.print(F("\" "));
    serialPrintln(currentProfileStep);
  }

  if (tuning)
    serialPrintln(F("A active"));
}

static void cmdExamineSettings()
{
  unsigned int crc16;
  serialPrint(F("EEPROM checksum: "));
  ospSettingsHelper::eepromRead(0, crc16);
  serialPrintln(crc16);

  if (modeIndex == AUTOMATIC)
    serialPrintln(F("PID mode"));
  else
    serialPrintln(F("MANUAL mode"));

  if (ctrlDirection == DIRECT)
    serialPrintln(F("Forward action"));
  else
    serialPrintln(F("Reverse action"));

  // write out the setpoints, with a '*' next to the active one
  for (byte i = 0; i < 4; i++)
  {
    if (i == setpointIndex)
      Serial.print('*');
    else
      Serial.print(' ');
    Serial.print(F("SP"));
    Serial.write('0' + i);
    Serial.print(F(": "));
    serialPrint(setPoints[i]);
    if (i % 2 == 0)
      Serial.print('\t');
    else
      Serial.print('\n');
  }

  Serial.println();

  serialPrintln(F("Comm speed (bps): "));
  serialPrintln(pgm_read_dword_near(&serialSpeedTable[serialSpeed]));

  serialPrint(F("Power-on: "));
  switch (powerOnBehavior)
  {
  case POWERON_DISABLE:
    serialPrintln(F("go to manual"));
    break;
  case POWERON_CONTINUE_LOOP:
    serialPrintln(F("hold last setpoint"));
    break;
  case POWERON_RESUME_PROFILE:
    serialPrintln(F("continue profile or hold last setpoint"));
    break;
  }

  Serial.println();

  // auto-tuner settings
  serialPrint(F("Auto-tuner ")); serialPrint(F("step size: "));
  serialPrintln(aTuneStep);
  serialPrint(F("Auto-tuner ")); serialPrint(F("noise size: "));
  serialPrintln(aTuneNoise);
  serialPrint(F("Auto-tuner ")); serialPrint(F("look-back: "));
  serialPrintln(aTuneLookBack);

  Serial.println();

  // peripheral card settings
  serialPrint(F("Input card "));
  serialPrintln(F("calibration data:"));
  for (byte i = 0; i < theInputCard.integerSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    serialPrintln(theInputCard.readIntegerSetting(i));
  }

  for (byte i = 0; i < theInputCard.floatSettingsCount(); i++)
  {
    Serial.print(F("  F"));
    serialPrint(i);
    Serial.print(F(": "));
    serialPrintln(theInputCard.readFloatSetting(i));
  }

  serialPrint(F("Output card "));
  serialPrintln(F("calibration data:"));  
  for (byte i = 0; i < theOutputCard.integerSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    serialPrintln(theOutputCard.readIntegerSetting(i));
  }

  for (byte i = 0; i < theOutputCard.floatSettingsCount(); i++)
  {
    Serial.print(F("  F"));
    serialPrint(i);
    Serial.print(F(": "));
    serialPrintln(theOutputCard.readFloatSetting(i));
  }
}

static void cmdExamineProfile(byte profileIndex)
{
  serialPrint(F("Profile "));
  Serial.print('0' + profileIndex);
  serialPrint(F(": "));

  for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
  {
    char ch = getProfileNameCharAt(profileIndex, i);
    if (ch == '\0')
      break;
    Serial.print(ch);
  }
  Serial.println();

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
  {
    byte type;
    unsigned long duration;
    double endpoint;

    getProfileStepData(profileIndex, i, &type, &duration, &endpoint);

    if (type == ospProfile::STEP_INVALID)
      break;
    serialPrint("  ");
    if (type < 10)
      Serial.print(' ');
    serialPrint(type);
    serialPrint(' ');
    serialPrint(duration);
    serialPrint(endpoint);
  }
}

// The command line parsing is table-driven, which saves more than 1.5 kB of code
// space.

enum {
  ARGS_NONE = 0,
  ARGS_INTEGER,
  ARGS_FLOAT,
  ARGS_FLOAT_FLOAT,
  ARGS_INTEGER_INTEGER,
  ARGS_INTEGER_INTEGER_FLOAT,
  ARGS_INTEGER_INTEGER_INTEGER,
  ARGS_FLOAT_FLOAT_INTEGER,
  ARGS_STRING,
  ARGS_NOT_FOUND = 0x7F,
  ARGS_FLAG_QUERYABLE = 0x80,
  ARGS_MASK = 0x7F
};

struct SerialCommandParseData {
  char mnemonic;
  byte args;
};

// FIXME: this table is now dense enough that it would be better to have
// two 26-byte arrays separately for the entire upper and lowercase alphabets

// this table must be sorted in ASCII order, that is A-Z then a-z
PROGMEM SerialCommandParseData commandParseData[] = {
  { 'A', ARGS_NONE },
  { 'B', ARGS_INTEGER_INTEGER_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'C', ARGS_NONE },
  { 'D', ARGS_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'E', ARGS_STRING },
  { 'I', ARGS_NONE },
  { 'K', ARGS_INTEGER },
  { 'L', ARGS_FLOAT_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'M', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'N', ARGS_STRING | ARGS_FLAG_QUERYABLE },
  { 'O', ARGS_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'P', ARGS_INTEGER_INTEGER_FLOAT },
  { 'Q', ARGS_NONE },
  { 'R', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'S', ARGS_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'T', ARGS_NONE | ARGS_FLAG_QUERYABLE },
  { 'V', ARGS_INTEGER },
  { 'X', ARGS_NONE },
  { 'a', ARGS_FLOAT_FLOAT_INTEGER },
  { 'b', ARGS_INTEGER_INTEGER_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'c', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'e', ARGS_INTEGER },
  { 'i', ARGS_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'k', ARGS_INTEGER_INTEGER },
  { 'l', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'n', ARGS_STRING },
  { 'o', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'p', ARGS_FLOAT | ARGS_FLAG_QUERYABLE },
  { 'r', ARGS_INTEGER },
  { 's', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 't', ARGS_INTEGER | ARGS_FLAG_QUERYABLE },
  { 'x', ARGS_INTEGER }
};

// perform a binary search for the argument descriptor of the given mnemonic
static byte argsForMnemonic(char mnemonic)
{
  byte start = 0, end = sizeof(commandParseData) / sizeof(commandParseData[0]) - 1;

  while (true)
  {
    if (start + 1 == end)
    {
      if (pgm_read_byte_near(&(commandParseData[start].mnemonic)) == mnemonic)
        return pgm_read_byte_near(&(commandParseData[start].args));
      if (pgm_read_byte_near(&(commandParseData[end].mnemonic)) == mnemonic)
        return pgm_read_byte_near(&(commandParseData[end].args));
      return ARGS_NOT_FOUND;
    }

    byte pivot = (start + end) / 2;
    char m = pgm_read_byte_near(&(commandParseData[pivot].mnemonic));
    if (m < mnemonic)
      start = pivot;
    else if (m > mnemonic)
      end = pivot;
    else // found it!
      return pgm_read_byte_near(&(commandParseData[pivot].args));
  }
}


// this is the entry point to the serial command processor: it is called
// when a '\n' has been received over the serial connection, and therefore
// a full command is buffered in serialCommandBuffer
static void processSerialCommand()
{
  const char *p = &serialCommandBuffer[1], *p2;
  double f1, f2;
  long i1, i2, i3;
  byte argDescriptor;

  if (serialCommandBuffer[--serialCommandLength] != '\n')
    goto out_EINV;

  // first parse the arguments
  argDescriptor = argsForMnemonic(serialCommandBuffer[0]);
  if (argDescriptor == ARGS_NOT_FOUND)
    goto out_EINV;

  if ((argDescriptor & ARGS_FLAG_QUERYABLE) && (*p == '?'))
  {
    // this is a query
    if (p[1] != '\n' && !(p[1] == '\r' && p[2] == '\n'))
      goto out_EINV;

    switch (serialCommandBuffer[0])
    {
    case 'a':
      serialPrintln(aTuneStep);
      serialPrintln(aTuneNoise);
      serialPrintln(aTuneLookBack);
      break;
    case 'c':
      serialPrintln(pgm_read_dword_near(&serialSpeedTable[serialSpeed]));
      break;
    case 'D':
      serialPrintln(kd);
      break;
    case 'i':
      serialPrintln(ki);
      break;
    case 'L':
      serialPrintln(lowerTripLimit);
      serialPrintln(upperTripLimit);
      break;
    case 'l':
      serialPrintln(tripLimitsEnabled);
      break;
    case 'M':
      serialPrintln(modeIndex);
      break;
    case 'N':
      serialPrintln(controllerName);
      break;
    case 'O':
      serialPrintln(output);
      break;
    case 'o':
      serialPrintln(powerOnBehavior);
      break;
    case 'p':
      serialPrintln(kp);
      break;
    case 'R':
      serialPrintln(ctrlDirection);
      break;
    case 'S':
      serialPrintln(setpoint);
      break;
    case 's':
      serialPrintln(setpointIndex);
      break;
    case 'T':
      serialPrintln(tripped);
      break;
    case 't':
      serialPrintln(tripAutoReset);
      break;
    default:
      goto out_EINV;
    }
    goto out_OK;
  }

  argDescriptor &= ARGS_MASK;

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

  // not a query, so parse it against the argDescriptor
  switch (argDescriptor)
  {
  case ARGS_NONE:
    CHECK_CMD_END();
    break;
  case ARGS_INTEGER_INTEGER_INTEGER: // i1, i2, i3
  case ARGS_INTEGER_INTEGER_FLOAT: // i1, i2, f1
    CHECK_SPACE();
    p2 = parseInt(p, &i1);
    CHECK_P2();
    // fall through
  case ARGS_INTEGER_INTEGER: // i2, i3
    CHECK_SPACE();
    p2 = parseInt(p, &i2);
    CHECK_P2();
    // fall through
  case ARGS_INTEGER: // i3
    CHECK_SPACE();
    if (argDescriptor == ARGS_INTEGER_INTEGER_FLOAT)
      p2 = parseFloat(p, &f1);
    else
      p2 = parseInt(p, &i3);
    CHECK_P2();
    CHECK_CMD_END();
    break;
  case ARGS_FLOAT_FLOAT: // f2, f1
    CHECK_SPACE();
    p2 = parseFloat(p, &f2);
  case ARGS_FLOAT: // f1
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_CMD_END();
    break;
  case ARGS_FLOAT_FLOAT_INTEGER: // f1, f2, i3
    CHECK_SPACE();
    p2 = parseFloat(p, &f1);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseFloat(p, &f2);
    CHECK_P2();
    CHECK_SPACE();
    p2 = parseInt(p, &i3);
    CHECK_P2();
    CHECK_CMD_END();
    break;
  case ARGS_STRING: // p
    CHECK_SPACE();
    // remove the trailing '\n' or '\r\n' from the #String
    if (serialCommandBuffer[serialCommandLength - 1] == '\r')
      serialCommandBuffer[--serialCommandLength] = '\0';
    else
      serialCommandBuffer[serialCommandLength] = '\0';
    // p now points to the #String
    break;
  default:
    BUGCHECK();
  }

#undef CHECK_CMD_END
#undef CHECK_SPACE
#undef CHECK_P2
#define BOUNDS_CHECK(f, min, max)                       \
  if ((f) < (min) || (f) > (max))                       \
    goto out_EINV;                                      \
  else do { } while (0)

  // arguments successfully parsed: try to execute the command
  switch (serialCommandBuffer[0])
  {
  case 'A': // start an auto-tune
    startAutoTune();
    goto out_OK; // no EEPROM writeback needed
  case 'a': // set the auto-tune parameters
    aTuneStep = f1;
    aTuneNoise = f2;
    aTuneLookBack = i3;
    break;
  case 'C': // cancel an auto-tune or profile execution
    if (tuning)
      stopAutoTune();
    else if (runningProfile)
      stopProfile();
    else
      goto out_EMOD;
    goto out_OK; // no EEPROM writeback needed
  case 'c': // set the comm speed
    if (cmdSetSerialSpeed(i3)) // since this resets the interface, just return
      return;
    goto out_EINV;
  case 'D': // set the D gain
    BOUNDS_CHECK(f1, 0, 100);
    if (tuning)
      goto out_EMOD;
    kd = f1;
    break;
  case 'E': // execute a profile by name
    if (!cmdStartProfile(p))
      goto out_EINV;
    goto out_OK; // no EEPROM writeback needed
  case 'e': // execute a profile by number
    BOUNDS_CHECK(i3, 0, NR_PROFILES-1);

    if (tuning || runningProfile || modeIndex != AUTOMATIC)
      goto out_EMOD;

    activeProfileIndex = i3;
    startProfile();
    goto out_OK; // no EEPROM writeback needed
  case 'I': // identify
    cmdIdentify();
    goto out_OK; // no EEPROM writeback needed
  case 'i': // set the I gain
    BOUNDS_CHECK(f1, 0, 100);

    if (tuning)
      goto out_EMOD;
    ki = f1;
    break;
  case 'K': // memory peek
    cmdPeek(i3);
    goto out_OK; // no EEPROM writeback needed
  case 'k': // memory poke
    BOUNDS_CHECK(i3, 0, 255);

    cmdPoke(i2, i3);
    goto out_OK; // no EEPROM writeback needed
  case 'L': // set trip limits
    BOUNDS_CHECK(f2, -999.9, 999.9);
    BOUNDS_CHECK(f1, -999.9, 999.9);

    lowerTripLimit = f2;
    upperTripLimit = f1;
    break;
  case 'l': // set limit trip enabled
    BOUNDS_CHECK(i3, 0, 1);
    tripLimitsEnabled = i3;
    break;
  case 'M': // set the controller mode (PID or manual)
    BOUNDS_CHECK(i3, 0, 1);

    modeIndex = i3;
    if (modeIndex == MANUAL)
      output = manualOutput;
    myPID.SetMode(i3);
    break;
  case 'N': // set the unit name
    if (strlen(p) > 16)
      goto out_EINV;

    strcpy(controllerName, p);
    break;
  case 'n': // clear and name the profile buffer
    if (strlen(p) > ospProfile::NAME_LENGTH)
      goto out_EINV;

    profileBuffer.clear();
    memset(profileBuffer.name, 0, sizeof(profileBuffer.name));
    strcpy(profileBuffer.name, p);
    break;
  case 'O': // directly set the output command
    BOUNDS_CHECK(f1, 0, 100);

    if (tuning || runningProfile || modeIndex != MANUAL)
      goto out_EMOD;

    output = f1;
    break;
  case 'o': // set power-on behavior
    BOUNDS_CHECK(i3, 0, 2);

    powerOnBehavior = i3;
    break;
  case 'P': // define a profile step
    if (!profileBuffer.addStep(i1, i2, f1))
      goto out_EINV;
    break;
  case 'p': // set the P gain
    BOUNDS_CHECK(f1, 0, 99.99);

    if (tuning)
      goto out_EMOD;
    kp = f1;
    break;
  case 'Q': // query current status
    cmdQuery();
    goto out_OK; // no EEPROM writeback needed
  case 'R': // set the controller action direction
    BOUNDS_CHECK(i3, 0, 1);

    ctrlDirection = i3;
    myPID.SetControllerDirection(i3);
    break;
  case 'r': // reset memory
    if (i3 != -999)
      goto out_EINV;

    clearEEPROM();
    Serial.println(F("Memory marked for reset."));
    Serial.println(F("Reset the unit to complete."));
    goto out_OK; // no EEPROM writeback needed or wanted!
  case 'S': // change the setpoint
    BOUNDS_CHECK(f1, -999.9, 999.9);

    if (tuning)
      goto out_EMOD;

    setPoints[setpointIndex] = f1;
    setpoint = f1;
    break;
  case 's': // change the active setpoint
    BOUNDS_CHECK(i3, 0, 3);

    setpointIndex = i3;
    break;
  case 'T': // clear a trip
    if (!tripped)
      goto out_EMOD;
    tripped = false;
    goto out_OK; // no EEPROM writeback needed
  case 't': // set trip auto-reset
    BOUNDS_CHECK(i3, 0, 1);
    tripAutoReset = i3;
    break;
  case 'V': // save the profile buffer to EEPROM
    BOUNDS_CHECK(i3, 0, 2);

    saveEEPROMProfile(i3);
    goto out_OK; // no EEPROM writeback needed
  case 'X': // examine: dump the controller settings
    cmdExamineSettings();
    goto out_OK; // no EEPROM writeback needed
  case 'x': // examine a profile: dump a description of the give profile
    BOUNDS_CHECK(i3, 0, 2);

    cmdExamineProfile(i3);
    goto out_OK; // no EEPROM writeback needed
  default:
    goto out_EINV;
  }

#undef BOUNDS_CHECK

  // we wrote a setting of some sort: schedule an EEPROM writeback
  markSettingsDirty();
out_OK:
  Serial.println(F("OK"));
  return;

out_EINV:
  Serial.println(F("EINV"));
  return;

out_EMOD:
  Serial.println(F("EMOD"));
  return;
}

