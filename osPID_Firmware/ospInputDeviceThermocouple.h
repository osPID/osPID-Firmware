#ifndef OSPINPUTDEVICETHERMOCOUPLE_H
#define OSPINPUTDEVICETHERMOCOUPLE_H

#include "ospInputDevice.h"
#include "ospSettingsHelper.h"
#include "MAX31855_local.h"

class ospInputDeviceThermocouple : 
  public ospInputDevice
{
private:
  enum { thermocoupleSO = A0  };
  enum { thermocoupleCS = A1  };
  enum { thermocoupleCLK = A2 };

  MAX31855 thermocouple;

public:
  ospInputDeviceThermocouple() :
    ospInputDevice(),
    thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO)
  { 
  }

  // setup the device
  void initialize() 
  {
    setInitialized(true);
  }

  // return the device identifier
  const __FlashStringHelper *deviceIdentifier()
  {
    return F("Thermocouple K");
  }

  // read the device
  double readInput() 
  {
    double val = thermocouple.readThermocouple(CELSIUS);
    if (val == FAULT_OPEN || val == FAULT_SHORT_GND || val == FAULT_SHORT_VCC)
      return NAN;
    return val + getCalibration();
  }

  // request input
  // returns conversion time in milliseconds
  unsigned long requestInput() 
  {
    return 0;
  }

  // how many settings does this device have
  byte floatSettingsCount() 
  {
    return 1; 
  }
  byte integerSettingsCount() 
  {
    return 0; 
  }

  // read settings from the device
  double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return getCalibration();
    default:
      return -1.0f;
    }
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
    switch (index) 
    {
    case 0:  
      setCalibration(val);
      return true;
    default:
      return false;
    }
  }
/*
  bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }
*/

  // describe the device settings
  const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return F("Calibration temperature adjustment (Celsius)");
    default:
      return false;
    }
  }
/*
  const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    switch (index) 
    {
    default:
      return false;
    }
  }
*/

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration = getCalibration();
    settings.save(tempCalibration);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    settings.restore(tempCalibration);
    setCalibration(tempCalibration);
  }
};


#endif
