/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "ospAssert.h"

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

  C #Integer -- Clear the memory if and only if the number is -999

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

  R #0-1 -- diRection set PID gain sign

  S #Number -- Setpoint -- change the (currently active) setpoint

  s #0-3 -- Select setpoint -- changes which setpoint is active

  V #0-2 -- saVe the profile buffer to profile N

  X -- dump the unit's settings

  x #0-2 -- dump profile N

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

// this is the entry point to the serial command processor: it is called
// when a '\n' has been received over the serial connection, and therefore
// a full command is buffered in serialCommandBuffer
void processSerialCommand()
{
  char *p = &serialCommandBuffer[1], *p2;

  if (serialCommandBuffer[--serialCommandLength] != '\n')
    goto out_EINV;

  switch (serialCommandBuffer[0])
  {
  case 'A':
  case 'a':
  case 'C':
  case 'c':
  case 'D':
  case 'E':
  case 'e':
  case 'I':
  case 'i':
  case 'K':
  case 'k':
  case 'L':
  case 'l':
  case 'M':
  case 'N':
  case 'n':
  case 'O':
  case 'P':
  case 'p':
  case 'Q':
  case 'R':
  case 'S':
  case 's':
  case 'V':
  case 'X':
  case 'x':
  default:
    goto out_EINV;
  }

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

