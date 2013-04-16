/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

#include <math.h>
#include <avr/pgmspace.h>
#include "ospAssert.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("COMM"), __LINE__);

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

PROGMEM int serialSpeedTable[7] = { 9600, 14400, 19200, 38400, 57600, 115200 };

void setupSerial()
{
  ospAssert(serialSpeed >= 0 && serialSpeed < 7);

  Serial.end();
  int kbps = pgm_read_word_near(&serialSpeedTable[serialSpeed]);
  Serial.begin(kbps);
}

// parse an int out of a string; returns a pointer to the first non-numeric
// character
const char * parseInt(const char *str, int *out)
{
  int value = 0;
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

// this is the entry point to the serial command processor: it is called
// when a '\n' has been received over the serial connection, and therefore
// a full command is buffered in serialCommandBuffer
void processSerialCommand()
{
}

