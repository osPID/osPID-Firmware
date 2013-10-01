#ifndef OSPOUTPUTDEVICESSR_H
#define OSPOUTPUTDEVICESSR_H

#include "ospIODevice.h"
#include "ospSettingsHelper.h"


enum { OUTPUT_SSR = 0 };
byte outputType = OUTPUT_SSR;

  

class ospOutputDeviceSsr : 
  public ospBaseOutputDevice 
{
private:
  enum { SSRPin = A3 };
  
  ospDecimalValue<1> outputWindowSeconds;
  unsigned long outputWindowMilliseconds;


public:
  ospOutputDeviceSsr() : 
    ospBaseOutputDevice(), 
    outputWindowMilliseconds(5000) // 5s OK for SSR depending on the load, needs to be longer for electromechanical relay
  { 
  }
  
  void initialize() 
  {
    pinMode(SSRPin, OUTPUT);
  }
  
  ospDecimalValue<1> getOutputWindowSeconds()
  {
    return outputWindowSeconds;
  }  
  
  void setOutputWindowSeconds(ospDecimalValue<1> newOutputWindowSeconds)
  {
    outputWindowSeconds = newOutputWindowSeconds;
    outputWindowMilliseconds = ((int)outputWindowSeconds) * 100;
  }  

  const __FlashStringHelper *IODeviceIdentifier() 
  { 
    return F("SSR Output"); 
  }

  // how many settings does this device have
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

  // read settings from the device
  double readFloatSetting(byte index) 
  {
    if (index == 0)
      return double(outputWindowSeconds);
    return NAN;
  }
/*
  int readIntegerSetting(byte index) 
  {
    return -1;
  }
*/

  // write settings to the device
  bool writeFloatSetting(byte index, double val) 
  {
    if (index == 0) 
    {
      this->setOutputWindowSeconds(makeDecimal<1>(val));
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
    return NULL;
  }
/*
  const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    return NULL;
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
    digitalWrite(SSRPin, (oVal > wind) ? HIGH : LOW);
  }
};


#endif
