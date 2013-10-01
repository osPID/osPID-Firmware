/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "ospConfig.h"
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

  L? #Number #Number -- set alarm lower and upper temperature Limits

  l? #0-1 -- enabLe or disabLe temperature limit

  M? #0-1 -- set the loop to manual/automatic Mode

  N #String -- clear the profile buffer and give it a Name

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

  t? #0-1 -- trip auto-reseT -- enable or disable automatic recovery from trips

  V #0-2 -- saVe the profile buffer to profile N

  W? -- query output Window size in seconds or set Window size

  X -- eXamine: dump the unit's settings

  x #0-2 -- eXamine profile: dump profile N

  Y -- identifY -- returns two lines: "osPid vX.YYtag" and "Unit {unitName}"
  
  
Removed codes: could maybe reinsert on Arduinos with sufficient flash memory

  K #Integer -- peeK at memory address, +ve number = SRAM, -ve number = EEPROM; returns
  the byte value in hexadecimal

  k #Integer #Integer -- poKe at memory address: the first number is the address,
  the second is the byte to write there, in decimal

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

// not static because called from elsewhere
void setupSerial()
{
  ospAssert((serialSpeed >= 0) && (serialSpeed < 7));

  Serial.end();
  unsigned int kbps = pgm_read_word_near(&serialSpeedTable[serialSpeed]);
  Serial.begin(kbps * 100);
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
    int rem  = val % divisor;

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

template<int D> static __attribute__ ((noinline)) ospDecimalValue<D> makeDecimal(long val, byte currentDecimals)
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

static void __attribute__ ((noinline)) serialPrintDecimal(int val, byte decimals)
{
  char buffer[8];
  char *p = formatDecimalValue(buffer, val, decimals);
  serialPrint(p);
}

template<int D> static void __attribute__ ((noinline)) serialPrint(ospDecimalValue<D> val)
{
  serialPrintDecimal(val.rawValue(), D);
}

template<int D> static void __attribute__ ((noinline)) serialPrintln(ospDecimalValue<D> val)
{
  serialPrint(val);
  Serial.println();
}

template<int D> static void __attribute__ ((noinline)) serialPrintlnTemp(ospDecimalValue<D> val)
{
  serialPrint(val);
#ifndef UNITS_FAHRENHEIT
  serialPrintln(FdegCelsius());
#else
  serialPrintln(FdegFahrenheit());
#endif
}

static void __attribute__ ((noinline)) serialPrintlnFloatTemp(double val)
{
  serialPrint(val);
#ifndef UNITS_FAHRENHEIT
  serialPrintln(FdegCelsius());
#else
  serialPrintln(FdegFahrenheit());
#endif
}

static void __attribute__ ((noinline)) serialPrintFAutotuner()
{
  serialPrint(F("Auto-tuner "));
}

static void __attribute__ ((noinline)) serialPrintFcolon()
{
  serialPrint(F(": "));
}

static void serialPrintDeviceFloatSettings(bool inputDevice)
{
  serialPrintln(F("put device settings:"));
  byte count = (inputDevice ? theInputDevice.floatSettingsCount() : theOutputDevice.floatSettingsCount());
  const __FlashStringHelper *description;
  for (byte i = 0; i < count; i++)
  {
    serialPrint(F("  "));
    serialPrint(char(i + '0'));
    serialPrintFcolon();
    description = (inputDevice ? theInputDevice.describeFloatSetting(i) : theOutputDevice.describeFloatSetting(i));
    serialPrint(description);
    serialPrint(F(" = "));
    serialPrintln(inputDevice ? theInputDevice.readFloatSetting(i) : theOutputDevice.readFloatSetting(i));
  }
}

static bool cmdSetSerialSpeed(const int& speed)
{
  for (byte i = 0; i < (sizeof(serialSpeedTable) / sizeof(serialSpeedTable[0])); i++)
  {
    unsigned int s = pgm_read_dword_near(&serialSpeedTable[i]);
    if (s == speed)
    {
      // this is a speed we can do: report success, and then reset the serial
      // interface to the new speed
      serialSpeed = i;
      serialPrintln(F("OK"));
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

    while (*p && (ch < ospProfile::NAME_LENGTH))
    {
      if (*p != getProfileNameCharAt(i, ch))
        match = false;
      p++;
      ch++;
    }

    if (match && (ch <= ospProfile::NAME_LENGTH))
    {
      // found the requested profile: start it
      activeProfileIndex = i;
      startProfile();
      return true;
    }
  }
  return false;
}

#ifndef ATMEGA_32kB_FLASH
static void cmdPeek(int address)
{
  byte val;

  if (address <= 0)
    ospSettingsHelper::eepromRead(-address, val);
  else
    val = * (byte *)address;

  serialPrint(hex(val >> 4));  
  serialPrintln(hex(val & 0xF));
}

static void cmdPoke(int address, byte val)
{
  if (address <= 0)
    ospSettingsHelper::eepromWrite(-address, val);
  else
    *(byte *)address = val;
}
#endif

static void cmdIdentify()
{
  serialPrint(F("Unit \""));
  serialPrintln(reinterpret_cast<const __FlashStringHelper *>(PcontrollerName));
  serialPrintln('"\nVersion ');
  serialPrintln(reinterpret_cast<const __FlashStringHelper *>(Pversion));
}

static void cmdQuery()
{
  serialPrint(F("S "));
  serialPrintlnFloatTemp(activeSetPoint);
  serialPrint(F("I "));
  serialPrintlnFloatTemp(input);
  serialPrint(F("O "));
  serialPrint(output);
  serialPrintln(F(" %"));

  if (runningProfile)
  {
    serialPrint(F("P \""));
    for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
    {
      char ch = getProfileNameCharAt(activeProfileIndex, i);

      if (ch == '\0')
        break;
      serialPrint(ch);
    }
    serialPrint(F("\" "));
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
    serialPrintln(F("Manual mode"));

  if (ctrlDirection == DIRECT)
    serialPrint(F("Direct action"));
  else
    serialPrint(F("Reverse action"));

  // write out the setpoints, with a '*' next to the active one
  for (byte i = 0; i < 4; i++)
  {
    if (i == setpointIndex)
      serialPrint('*');
    else
      serialPrint(' ');
    serialPrint(F("SP"));
    serialPrint(char('1' + i));
    serialPrintFcolon();
    serialPrint(setPoints[i]);
#ifndef UNITS_FAHRENHEIT
    serialPrint(FdegCelsius());
#else
    serialPrint(FdegFahrenheit());
#endif
    if (i & 1 == 0)
      serialPrint('\t');
    else
      serialPrint('\n');
  }

  Serial.println();

  serialPrint(F("Comm speed (bps): "));
  serialPrint(pgm_read_word_near(&serialSpeedTable[serialSpeed]));
  serialPrintln("00");

  serialPrint(F("Power-on: "));
  switch (powerOnBehavior)
  {
  case POWERON_DISABLE:
    serialPrintln(F("go to manual"));
    break;
  case POWERON_RESUME_PROFILE:
    serialPrint(F("continue profile or "));
    // run on into next case with no break;
  case POWERON_CONTINUE_LOOP:
    serialPrintln(F("hold last setpoint"));
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
  serialPrint(F("In"));
  serialPrintDeviceFloatSettings(true);
  // same for integer settings, if any

  serialPrint(F("Out"));
  //serialPrintFCalibrationData();  
  serialPrintDeviceFloatSettings(false);
  // same for integer settings, if any
}

static void cmdExamineProfile(byte profileIndex)
{
  serialPrint(reinterpret_cast<const __FlashStringHelper *>(Pprofile));
  serialPrint(char('0' + profileIndex));
  serialPrintFcolon();

  for (byte i = 0; i < ospProfile::NAME_LENGTH; i++)
  {
    char ch = getProfileNameCharAt(profileIndex, i);
    if (ch == '\0')
      break;
    serialPrint(ch);
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

static bool trySetTemp(ospDecimalValue<1> *p, long val, byte decimals)
{
  ospDecimalValue<1> temp = makeDecimal<1>(val, decimals);

  if (temp > (ospDecimalValue<1>){9999} || temp < (ospDecimalValue<1>){-9999})
    return false;

  *p = temp;
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
#ifndef ATMEGA_32kB_FLASH
  { 'K', ARGS_ONE_NUMBER },
#endif
  { 'L', ARGS_TWO_NUMBERS | ARGS_FLAG_QUERYABLE },
  { 'M', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
  { 'N', ARGS_STRING },
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
#ifndef ATMEGA_32kB_FLASH
  { 'k', ARGS_TWO_NUMBERS },
#endif
  { 'l', ARGS_ONE_NUMBER | ARGS_FLAG_FIRST_IS_01 | ARGS_FLAG_QUERYABLE },
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
      serialPrintlnTemp(theInputDevice.getCalibration());
      break;
    case 'c':
      serialPrint(pgm_read_word_near(&serialSpeedTable[serialSpeed]));
      serialPrintln("00");
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
      serialPrintlnTemp(lowerTripLimit);
      serialPrintlnTemp(upperTripLimit);
      break;
    case 'l':
      serialPrintln(tripLimitsEnabled);
      break;
    case 'M':
      serialPrintln(modeIndex);
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
      serialPrintlnFloatTemp(activeSetPoint);
      break;
    case 's':
      serialPrintln(setpointIndex);
      break;
    case 'T':
      serialPrintln(tripped);
      break;
    case 'W':
      serialPrint(theOutputDevice.getOutputWindowSeconds());
      serialPrintln(" seconds");
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
    theInputDevice.setCalibration(cal);
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
#ifndef ATMEGA_32kB_FLASH  
  case 'K': // memory peek
    cmdPeek(i1);
    goto out_OK; // no EEPROM writeback needed
  case 'k': // memory poke
    BOUNDS_CHECK(i1, 0, 255);

    cmdPoke(i2, i1);
    goto out_OK; // no EEPROM writeback needed
#endif    
  case 'L': // set trip limits
    {
      ospDecimalValue<1> lower;
      if (!trySetTemp(&lower, i2, d1))
        goto out_EINV;
      if (!trySetTemp(&upperTripLimit, i1, d1))
        goto out_EINV;
      lowerTripLimit = lower;
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
  case 'N': // clear and name the profile buffer
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

      manualOutput = double(o);
      output = manualOutput;
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
    serialPrintln(F("Memory marked for reset."));
    serialPrintln(F("Reset the unit to complete."));
    goto out_OK; // no EEPROM writeback needed or wanted!
  case 'S': // change the setpoint
    {
      if (tuning)
        goto out_EMOD;
      if (!trySetTemp(&setPoints[setpointIndex], i1, d1))      
        goto out_EINV;
      activeSetPoint = double(setPoints[setpointIndex]);
    }
    break;
  case 's': // change the active setpoint
    if (i1 >= 4)
      goto out_EINV;

    setpointIndex = i1;
    activeSetPoint = double(setPoints[setpointIndex]);
    break;
  case 'T': // clear a trip
    if (!tripped)
      goto out_EMOD;
    tripped = false;
#ifndef SILENCE_BUZZER    
    buzzOff;
#endif    
    goto out_OK; // no EEPROM writeback needed
  case 't': // set trip auto-reset
    tripAutoReset = i1;
    break;
  case 'V': // save the profile buffer to EEPROM
    saveEEPROMProfile(i1);
    goto out_OK; // no EEPROM writeback needed
  case 'W': // set the output window size in seconds
    ospDecimalValue<1> window;
    window = makeDecimal<1>(i1, d1);
    BOUNDS_CHECK(window, (ospDecimalValue<1>){10}, (ospDecimalValue<1>){9999});
    theOutputDevice.setOutputWindowSeconds(window);
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
  serialPrintln(F("OK"));
  return;

out_EINV:
  serialPrintln(F("EINV"));
  return;

out_EMOD:
  serialPrintln(F("EMOD"));
  return;
}

