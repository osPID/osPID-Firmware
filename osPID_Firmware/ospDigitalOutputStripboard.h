#ifndef OSPDIGITALOUTPUTSTRIPBOARD_H
#define OSPDIGITALOUTPUTSTRIPBOARD_H

#include "ospCards.h"

class ospDigitalOutputCard : 
public ospBaseOutputCard 
{
private:
  enum { 
    SSRPin = A3   };
  enum { 
    OUTPUT_SSR = 1   };

  byte outputType;
  double outputWindowSeconds;
  unsigned long outputWindowMilliseconds;

public:
  ospDigitalOutputCard() 
: 
    ospBaseOutputCard(),
    outputType(OUTPUT_SSR),
    outputWindowSeconds(5.0),
    outputWindowMilliseconds(5000)
    { 
    }

  void initialize() 
  {
    pinMode(SSRPin, OUTPUT);
  }

  const __FlashStringHelper *cardIdentifier() 
  { 
    return F("OUT_DIGITAL"); 
  }

  // how many settings does this card have
  byte floatSettingsCount()
  { 
    return 1; 
  }
  byte integerSettingsCount() 
  { 
    return 1; 
  }

  // read settings from the card
  double readFloatSetting(byte index) 
  {
    if (index == 0)
      return outputWindowSeconds;
    return -1.0f;
  }

  int readIntegerSetting(byte index) 
  {
    if (index == 0)
      return outputType;
    return -1;
  }

  // write settings to the card
  bool writeFloatSetting(byte index, double val) 
  {
    if (index == 0) 
    {
      outputWindowSeconds = val;
      outputWindowMilliseconds = round(outputWindowSeconds * 1000.0f);
      return true;
    }
    return false;
  }

  // describe the available settings
  const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    if (index == 0) 
      return F("Output type = SSR (1)");
    return 0;
  }

  const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    if (index == 0) 
      return F("Output PWM cycle length in milliseconds");
    return 0;
  }

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    settings.save(outputType);
    settings.save(outputWindowMilliseconds);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    settings.restore(outputType);
    settings.restore(outputWindowMilliseconds);
  }

  void setOutputPercent(double percent) 
  {
    unsigned long wind = millis() % outputWindowMilliseconds;
    unsigned long oVal = (unsigned long)(percent * 0.01 * (double)outputWindowMilliseconds);
    digitalWrite(SSRPin, (oVal>wind) ? HIGH : LOW);
  }
};

typedef ospDigitalOutputCard ospDigitalOutputStripboardV1_0;

#endif


