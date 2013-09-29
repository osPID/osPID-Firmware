#ifndef OSPDIGITALOUTPUTCARD_H
#define OSPDIGITALOUTPUTCARD_H

#include "ospCards.h"
#include "ospSettingsHelper.h"

class ospDigitalOutputCard : 
public ospBaseOutputCard 
{
private:
  enum { SSRPin = A3 };
  
  double outputWindowSeconds;
  unsigned long outputWindowMilliseconds;


public:
  ospDigitalOutputCard() : 
    ospBaseOutputCard(),
    outputWindowSeconds(5.0), // 5s OK for SSR depending on the load, needs to be longer for electromechanical relay
    outputWindowMilliseconds(5000)
  { 
  }
  
  void initialize() 
  {
    pinMode(SSRPin, OUTPUT);
  }
  
  double outputWindowSeconds()
  {
    return outputWindowSeconds;
  }  
  
  void setOutputWindowSeconds(double newOutputWindowSeconds)
  {
    outputWindowSeconds = newOutputWindowSeconds;
  }  

  const __FlashStringHelper *cardIdentifier() 
  { 
    return F("SSR Output"); 
  }

  // how many settings does this card have
  byte floatSettingsCount()
  { 
    return 1; 
  }
/*
  byte integerSettingsCount() 
  { 
    return 0; 
  }
*/

  // read settings from the card
  double readFloatSetting(byte index) 
  {
    if (index == 0)
      return outputWindowSeconds;
    return -1.0f;
  }
/*
  int readIntegerSetting(byte index) 
  {
    return -1;
  }
*/

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
/*
  bool writeIntegerSetting(byte index, int val)
  {
    return false;
  } 
*/

  // describe the available settings
  const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    if (index == 0) 
      return F("Output PWM cycle length in milliseconds");
    return 0;
  }
/*
  const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    return 0;
  }
*/

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    settings.save(outputWindowMilliseconds);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    settings.restore(outputWindowMilliseconds);
  }

  void setOutputPercent(double percent)
  {
    unsigned long wind = millis() % outputWindowMilliseconds;
    unsigned long oVal = (unsigned long)(percent * 0.01 * (double)outputWindowMilliseconds);
    digitalWrite(SSRPin, (oVal>wind) ? HIGH : LOW);
  }
};


#endif
