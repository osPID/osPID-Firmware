/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "defines.h"
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

  B? #Number -- query / set input device temperature caliBration value

  c? #Integer -- set the Comm speed, in kbps

  D? #Number -- set D gain

  E #String -- Execute the profile of the given name

  e #0-2 -- Execute the profile of the given number

  I? #0-3 -- query or set Input sensor

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

  P #Integer #Integer #Number -- add a step to the profile buffer with the
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

  U? #CF -- query temperature Units or set Units to Celsius or Fahrenheit

  t? #0-1 -- trip auto-reseT -- enable or disable automatic recovery from trips

  V #0-2 -- saVe the profile buffer to profile N

  W? -- query output Window size in seconds or set Window size

  X -- eXamine: dump the unit's settings

  x #0-2 -- eXamine profile: dump profile N

  Y -- identifY -- returns two lines: "osPid vX.YYtag" and "Unit {unitName}"

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

enum 
{
  SERIAL_SPEED_9p6k = 0,
  SERIAL_SPEED_14p4k = 1,
  SERIAL_SPEED_19p2k = 2,
  SERIAL_SPEED_28p8k = 3,
  SERIAL_SPEED_38p4k = 4,
  SERIAL_SPEED_57p6k = 5,
  SERIAL_SPEED_115k = 6
};

byte serialSpeed = SERIAL_SPEED_28p8k;


static void setupSerial()
{
  ospAssert(serialSpeed >= 0 && serialSpeed < 7);

  Serial.end();
  long int kbps = pgm_read_dword_near(&serialSpeedTable[serialSpeed]);
  Serial.begin(kbps);
}

static const char * parseDecimal(const char *str, long *out, byte *decimals)
{
  long value = 0;
  byte dec = 0;

  bool isNegative = false;
  bool hasFraction = false;

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

      *out = value;
      *decimals = dec;
      return str;
    }

    str++;
    value = value * 10 + (c - '0');

    if (hasFraction)
      dec++;
  }
}

static int pow10(byte n)
{
  int result = 1;

  while (n--)
    result *= 10;

  return result;
}

static int coerceToDecimal(long val, byte currentDecimals, byte desiredDecimals)
{
  if (currentDecimals < desiredDecimals)
    return int(val * pow10(desiredDecimals - currentDecimals));
  else if (desiredDecimals < currentDecimals)
  {
    // need to do a rounded division
    int divisor = pow10(currentDecimals - desiredDecimals);
    int quot = val / divisor;
    int rem = val % divisor;

    if (abs(rem) >= divisor / 2)
    {
      if (val < 0)
        quot--;
      else
        quot++;
    }
    return quot;
  }
  else
    return int(val);
}

template<int D> static ospDecimalValue<D> makeDecimal(long val, byte currentDecimals)
{
  return (ospDecimalValue<D>) {coerceToDecimal(val, currentDecimals, D)};
}

// since the serial buffer is finite, we perform a realtime loop iteration
// between each serial write
template<typename T> static void __attribute__((noinline)) serialPrint(T t)
{
  Serial.print(t);
  realtimeLoop();
}

template<typename T> static void __attribute__((noinline)) serialPrintln(T t)
{
  Serial.print(t);
  Serial.println();
  realtimeLoop();
}

static void serialPrintDecimal(int val, byte decimals)
{
  char buffer[8];
  char *p = formatDecimalValue(buffer, val, decimals);
  serialPrint(p);
}

template<int D> static void serialPrint(ospDecimalValue<D> val)
{
  serialPrintDecimal(val.rawValue(), D);
}

template<int D> static void serialPrintln(ospDecimalValue<D> val)
{
  serialPrintDecimal(val.rawValue(), D);
  Serial.println();
}

template<int D> static void serialPrintTempln(ospDecimalValue<D> val)
{
  serialPrintDecimal(val.rawValue(), D);
  Serial.print(" \337");
  Serial.write(displayCelsius ? 'C' : 'F');
  Serial.println();
}

static void serialPrintFloatTempln(double val)
{
  Serial.print(val);
  Serial.print(" \337");
  Serial.write(displayCelsius ? 'C' : 'F');
  Serial.println();
}

void serialPrintFAutotuner()
{
  serialPrint(F("Auto-tuner "));
}
 
/*
void serialPrintFCalibrationData()
{
  serialPrintln(F("calibration data:"));
}
*/

static bool cmdSetSerialSpeed(const long& speed)
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

  if (address <= 0)
    ospSettingsHelper::eepromRead(-address, val);
  else
    val = * (byte *)address;

  Serial.print(hex(val >> 4));  
  serialPrintln(hex(val & 0xF));
}

static void cmdPoke(int address, byte val)
{
  if (address <= 0)
    ospSettingsHelper::eepromWrite(-address, val);
  else
    *(byte *)address = val;
}

static void cmdIdentify()
{
  serialPrintln(F("osPID " OSPID_VERSION_TAG));
  serialPrint(F("Unit \""));
  Serial.print(controllerName);
  serialPrintln('"');
}

static void cmdQuery()
{
  Serial.print(F("S "));
  serialPrintFloatTempln(displayUnits(activeSetPoint));
  Serial.print(F("I "));
  serialPrintFloatTempln(displayUnits(input));
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
    Serial.write('1' + i);
    Serial.print(F(": "));
    serialPrint(setPoints[i]);
    Serial.print(" /337");
    Serial.write(displayCelsius ? 'C' : 'F');
    if (i & 1 == 0)
      Serial.print('\t');
    else
      Serial.print('\n');
  }

  Serial.println();

  serialPrint(F("Comm speed (bps): "));
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
  serialPrintFAutotuner(); serialPrint(F("step size: "));
  serialPrintln(aTuneStep);
  serialPrintFAutotuner(); serialPrint(F("noise size: "));
  serialPrintln(aTuneNoise);
  serialPrintFAutotuner(); serialPrint(F("look-back: "));
  serialPrintln(aTuneLookBack);

  Serial.println();

  // peripheral device settings
  serialPrint(F("Input device "));
  //serialPrintFCalibrationData();
/*
  for (byte i = 0; i < theInputDevice->integerSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    const __FlashStringHelper *description = theInputDevice->describeIntegerSetting(i);
    serialPrint(description);
    Serial.print(F(" = "));
    serialPrintln(theInputDevice->readIntegerSetting(i));
    Serial.println();
  }
*/
  for (byte i = 0; i < theInputDevice->floatSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    const __FlashStringHelper *description = theInputDevice->describeFloatSetting(i);
    serialPrint(description);
    Serial.print(F(" = "));
    serialPrintln(theInputDevice->readFloatSetting(i));
    Serial.println();
  }

  serialPrint(F("Output device "));
  //serialPrintFCalibrationData();  
/*
  for (byte i = 0; i < theOutputDevice->integerSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    const __FlashStringHelper *description = theOutputDevice->describeIntegerSetting(i);
    serialPrint(description);
    Serial.print(F(" = "));
    serialPrintln(theOutputDevice->readIntegerSetting(i));
    Serial.println();
  }
*/
  for (byte i = 0; i < theOutputDevice->floatSettingsCount(); i++)
  {
    Serial.print(F("  I"));
    serialPrint(i);
    Serial.print(F(": "));
    const __FlashStringHelper *description = theOutputDevice->describeFloatSetting(i);
    serialPrint(description);
    Serial.print(F(" = "));
    serialPrintln(theOutputDevice->readFloatSetting(i));
    Serial.println();
  }
}

static void cmdExamineProfile(byte profileIndex)
{
  serialPrint(F("Profile "));
  Serial.print(char('0' + profileIndex));
  serialPrint(F(": "));

  for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
  {
    char ch = getProfileNameCharAt(profileIndex, i);
    if (ch == '\0')
      break;
    Serial.print(ch);
  }
  Serial.println();

  serialPrint(F("Checksum: "));
  serialPrintln(getProfileCrc(profileIndex));

  for (byte i = 0; i < ospProfile::NR_STEPS; i++)
  {
    byte type;
    unsigned long duration;
    ospDecimalValue<1> endpoint;

    getProfileStepData(profileIndex, i, &type, &duration, &endpoint);

    if (type == ospProfile::STEP_INVALID)
      break;
    if (type & ospProfile::STEP_FLAG_BUZZER)
      serialPrint(F(" *"));
    else
      serialPrint(F("  "));
    serialPrint(type & ospProfile::STEP_TYPE_MASK);
    serialPrint(' ');
    serialPrint(duration);
    serialPrint(' ');
    serialPrintln(endpoint);
  }
}

static bool trySetGain(ospDecimalValue<3> *p, long val, byte decimals)
{
  ospDecimalValue<3> gain = makeDecimal<3>(val, decimals);

  if (gain > (ospDecimalValue<3>){32767} || gain < (ospDecimalValue<3>){0})
    return false;

  *p = gain;
  return true;
}

// The command line parsing is table-driven, which saves more than 1.5 kB of code
// space.

enum {
  ARGS_NONE = 0,
  ARGS_ONE_NUMBER,
  ARGS_TWO_NUMBERS,
  ARGS_THREE_NUMBERS,
  ARGS_STRING,
  ARGS_NOT_FOUND = 0x0F,
  ARGS_FLAG_PROFILE_NUMBER = 0x10, // must be 0 to NR_PROFILES-1
  ARGS_FLAG_FIRST_IS_01 = 0x20, // must be 0 or 1
  ARGS_FLAG_NONNEGATIVE = 0x40, // must be >= 0
  ARGS_FLAG_QUERYABLE = 0x80,
  ARGS_MASK = 0x0F
};

struct SerialCommandParseData 
{
  char mnemonic;
  byte args;
};

// FIXME: this table is now dense enough that it would be better to have
// two 26-byte arrays separately for the entire upper and lowercase alphabets

// this table must be sorted in ASCII order, that is A-Z then a-z
PROGMEM SerialCommandParseData commandParseData[] = 
{
  { 'A', ARGS_NONE },
  { 'B', ARGS_ONE_NUMBER | ARGS_FLAG_QUERYABLE },
  { 'C', ARGS_NONE },
  { 'D', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'E', ARGS_STRING },
  { 'I', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'K', ARGS_ONE_NUMBER },
  { 'L', ARGS_TWO_NUMBERS | ARGS_FLAG_QUERYABLE },
  { 'M', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'N', ARGS_STRING | ARGS_FLAG_QUERYABLE },
  { 'O', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'P', ARGS_THREE_NUMBERS },
  { 'Q', ARGS_NONE },
  { 'R', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'S', ARGS_ONE_NUMBER | ARGS_FLAG_QUERYABLE },
  { 'T', ARGS_NONE | ARGS_FLAG_QUERYABLE },
  { 'U', ARGS_STRING },
  { 'V', ARGS_ONE_NUMBER | ARGS_FLAG_PROFILE_NUMBER },
  { 'W', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'X', ARGS_NONE },
  { 'Y', ARGS_NONE },
  { 'a', ARGS_THREE_NUMBERS },
  { 'b', ARGS_THREE_NUMBERS | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'c', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'e', ARGS_ONE_NUMBER | ARGS_FLAG_PROFILE_NUMBER },
  { 'i', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'k', ARGS_TWO_NUMBERS },
  { 'l', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'n', ARGS_STRING },
  { 'o', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'p', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 'r', ARGS_ONE_NUMBER },
  { 's', ARGS_ONE_NUMBER | ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_QUERYABLE },
  { 't', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'x', ARGS_ONE_NUMBER | ARGS_FLAG_PROFILE_NUMBER }
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
  long i1, i2, i3;
  byte d1, d2, d3;
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
    case 'B':
      serialPrintFloatTempln(theInputDevice->getCalibration() / (displayCelsius ? 1.0 : 1.8));
    case 'c':
      serialPrintln(pgm_read_dword_near(&serialSpeedTable[serialSpeed]));
      break;
    case 'D':
      serialPrintln(DGain);
      break;
    case 'I':
      serialPrintln(inputType);
      break;
    case 'i':
      serialPrintln(IGain);
      break;
    case 'L':
      serialPrintTempln(lowerTripLimit);
      serialPrintTempln(upperTripLimit);
      break;
    case 'l':
      serialPrintln(tripLimitsEnabled);
      break;
    case 'M':
      serialPrintln(modeIndex);
      break;
    case 'N':
      Serial.print(controllerName);
      Serial.println();
      break;
    case 'O':
      serialPrintln(output);
      break;
    case 'o':
      serialPrintln(powerOnBehavior);
      break;
    case 'p':
      serialPrintln(PGain);
      break;
    case 'R':
      serialPrintln(ctrlDirection);
      break;
    case 'S':
      serialPrintFloatTempln(displayUnits(activeSetPoint));
      break;
    case 's':
      serialPrintln(setpointIndex);
      break;
    case 'T':
      serialPrintln(tripped);
      break;
    case 'U':
      serialPrintln(displayCelsius ? "Celsius" : "Fahrenheit");
    case 'W':
      Serial.print(theOutputDevice->getOutputWindowSeconds());
      Serial.print(" seconds");
      Serial.println();
      break;
    case 't':
      serialPrintln(tripAutoReset);
      break;
    default:
      goto out_EINV;
    }
    goto out_OK;
  }

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
  switch (argDescriptor & ARGS_MASK)
  {
  case ARGS_NONE:
    CHECK_CMD_END();
    break;
  case ARGS_THREE_NUMBERS: // i3, i2, i1
    CHECK_SPACE();
    p2 = parseDecimal(p, &i3, &d3);
    CHECK_P2();
    // fall through
  case ARGS_TWO_NUMBERS: // i2, i1
    CHECK_SPACE();
    p2 = parseDecimal(p, &i2, &d2);
    CHECK_P2();
    // fall through
  case ARGS_ONE_NUMBER: // i1
    CHECK_SPACE();
    p2 = parseDecimal(p, &i1, &d1);
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

  // perform bounds checking
  if (argDescriptor & (ARGS_FLAG_NONNEGATIVE | ARGS_FLAG_PROFILE_NUMBER | ARGS_FLAG_FIRST_IS_01))
  {
    if (i1 < 0)
      goto out_EINV;
  }

  if ((argDescriptor & ARGS_FLAG_PROFILE_NUMBER) && i1 >= NR_PROFILES)
    goto out_EINV;

  if ((argDescriptor & ARGS_FLAG_FIRST_IS_01) && i1 > 1)
    goto out_EINV;

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
    aTuneStep = makeDecimal<1>(i3, d3);
    aTuneNoise = makeDecimal<1>(i2, d2);
    aTuneLookBack = i1;
    break;
  case 'B':
    ospDecimalValue<1> cal;
    cal = makeDecimal<1>(i1, d1);
    BOUNDS_CHECK(cal, (ospDecimalValue<1>){-999}, (ospDecimalValue<1>){999});
    theInputDevice->setCalibration(double(cal));
    displayCalibration = cal;
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
    if (cmdSetSerialSpeed(i1)) // since this resets the interface, just return
      return;
    goto out_EINV;
  case 'D': // set the D gain
    if (tuning)
      goto out_EMOD;
    if (!trySetGain(&DGain, i1, d1))
      goto out_EINV;
    break;
  case 'E': // execute a profile by name
    if (!cmdStartProfile(p))
      goto out_EINV;
    goto out_OK; // no EEPROM writeback needed
  case 'e': // execute a profile by number
    if (tuning || runningProfile || modeIndex != AUTOMATIC)
      goto out_EMOD;

    activeProfileIndex = i1;
    startProfile();
    goto out_OK; // no EEPROM writeback needed
  case 'I': // set the inputType
    BOUNDS_CHECK(i1, 0, 2);
    inputType = i1;
    break;
  case 'i': // set the I gain
    if (tuning)
      goto out_EMOD;
    if (!trySetGain(&IGain, i1, d1))
      goto out_EINV;
    break;
  case 'K': // memory peek
    cmdPeek(i1);
    goto out_OK; // no EEPROM writeback needed
  case 'k': // memory poke
    BOUNDS_CHECK(i1, 0, 255);

    cmdPoke(i2, i1);
    goto out_OK; // no EEPROM writeback needed
  case 'L': // set trip limits
    {
      ospDecimalValue<1> lower = makeDecimal<1>(i2, d2);
      ospDecimalValue<1> upper = makeDecimal<1>(i1, d1);
      BOUNDS_CHECK(lower, (ospDecimalValue<1>){-9999}, (ospDecimalValue<1>){9999});
      BOUNDS_CHECK(upper, (ospDecimalValue<1>){-9999}, (ospDecimalValue<1>){9999});

      lowerTripLimit = lower;
      upperTripLimit = upper;
    }
    break;
  case 'l': // set limit trip enabled
    tripLimitsEnabled = i1;
    break;
  case 'M': // set the controller mode (PID or manual)
    modeIndex = i1;
    if (modeIndex == MANUAL)
      output = manualOutput;
    myPID.SetMode(i1);
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
    {
      ospDecimalValue<1> o = makeDecimal<1>(i1, d1);
      if (o > (ospDecimalValue<1>){1000})
        goto out_EINV;

      if (tuning || runningProfile || modeIndex != MANUAL)
        goto out_EMOD;

      manualOutput = o;
      output = double(o);
    }
    break;
  case 'o': // set power-on behavior
    if (i1 > 2)
      goto out_EINV;
    powerOnBehavior = i1;
    break;
  case 'P': // define a profile step
    if (!profileBuffer.addStep(i3, i2, makeDecimal<1>(i1, d1)))
      goto out_EINV;
    break;
  case 'p': // set the P gain
    if (tuning)
      goto out_EMOD;
    if (!trySetGain(&PGain, i1, d1))
      goto out_EINV;
    break;
  case 'Q': // query current status
    cmdQuery();
    goto out_OK; // no EEPROM writeback needed
  case 'R': // set the controller action direction
    ctrlDirection = i1;
    myPID.SetControllerDirection(i1);
    break;
  case 'r': // reset memory
    if (i1 != -999)
      goto out_EINV;

    clearEEPROM();
    Serial.println(F("Memory marked for reset."));
    Serial.println(F("Reset the unit to complete."));
    goto out_OK; // no EEPROM writeback needed or wanted!
  case 'S': // change the setpoint
    {
      ospDecimalValue<1> sp = makeDecimal<1>(i1, d1);
      BOUNDS_CHECK(sp, (ospDecimalValue<1>){-9999}, (ospDecimalValue<1>){9999});

      if (tuning)
        goto out_EMOD;

      setPoints[setpointIndex] = sp;
      activeSetPoint = celsius(double(sp));
    }
    break;
  case 's': // change the active setpoint
    if (i1 >= 4)
      goto out_EINV;

    setpointIndex = i1;
    activeSetPoint = celsius(double(setPoints[setpointIndex]));
    break;
  case 'T': // clear a trip
    if (!tripped)
      goto out_EMOD;
    tripped = false;
    noTone( buzzerPin );
    goto out_OK; // no EEPROM writeback needed
  case 't': // set trip auto-reset
    tripAutoReset = i1;
    break;
  case 'U': // change temperature units
    if ((*p == 'C') || (*p == 'c'))
      displayCelsius = true;
    else if ((*p == 'F') || (*p == 'f'))
      displayCelsius = false;
    else
      goto out_EINV;
    break;
  case 'V': // save the profile buffer to EEPROM
    saveEEPROMProfile(i1);
    goto out_OK; // no EEPROM writeback needed
  case 'W': // set the output window size in seconds
    ospDecimalValue<1> window;
    window = makeDecimal<1>(i1, d1);
    BOUNDS_CHECK(window, (ospDecimalValue<1>){10}, (ospDecimalValue<1>){9999});
    theOutputDevice->setOutputWindowSeconds(double(window));
    displayWindow = window;
    break;
  case 'X': // examine: dump the controller settings
    cmdExamineSettings();
    goto out_OK; // no EEPROM writeback needed
  case 'x': // examine a profile: dump a description of the give profile
    cmdExamineProfile(i1);
    goto out_OK; // no EEPROM writeback needed
  case 'Y': // identify
    cmdIdentify();
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

