#ifndef OSPINPUTDEVICEONEWIRE_H
#define OSPINPUTDEVICEONEWIRE_H

#include "ospInputDevice.h"
#include "ospSettingsHelper.h"
#include "OneWire_local.h"
#include "DallasTemperature_local.h"

class ospInputDeviceOneWire : 
  public ospInputDevice 
{
private:
  enum { oneWireBus = A0 };

  OneWire oneWire;
  DallasTemperature oneWireDevice;
  DeviceAddress oneWireDeviceAddress;


public:
  ospInputDeviceOneWire() :
    ospInputDevice(),
    oneWire(oneWireBus),
    oneWireDevice(&oneWire)
  { 
  }

  // setup the device
  void initialize() 
  {
    oneWireDevice.begin();
    if (!oneWireDevice.getAddress(oneWireDeviceAddress, 0)) 
    {
      setInitialized(false);
    }
    else 
    {
      oneWireDevice.setWaitForConversion(false);
      oneWireDevice.setResolution(oneWireDeviceAddress, 12);
      setInitialized(true);
    }
  }

  // return the device identifier
  const __FlashStringHelper *deviceIdentifier()
  {
    return F("DS18B20+");
  }

public:
  // request input
  // returns conversion time in milliseconds
  unsigned long requestInput() 
  {
    oneWireDevice.requestTemperatures();
    return 750;
  }

  // read the device
  double readInput() 
  {
    return oneWireDevice.getTempCByIndex(0) + getCalibration(); 
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
    switch (index) 
    {
    case 0:
      return calibration();
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
    double tempCalibration = calibration;
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
