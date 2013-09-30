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
  virtual void initialize() 
  {
    oneWireDevice.begin();
    if (!oneWireDevice.getAddress(oneWireDeviceAddress, 0)) 
    {
      this->setInitializationStatus(false);
    }
    else 
    {
      oneWireDevice.setResolution(oneWireDeviceAddress, 12);
      oneWireDevice.setWaitForConversion(false);
      this->setInitializationStatus(true);
    }
  }

  // return the device identifier
  virtual const __FlashStringHelper *IODeviceIdentifier()
  {
    return F("DS18B20+");
  }

public:
  // request input
  // returns conversion time in milliseconds
  virtual unsigned long requestInput() 
  {
    oneWireDevice.requestTemperatures();
    return 750;
  }

  // read the device
  virtual double readInput() 
  {
    return oneWireDevice.getTempCByIndex(0) + this->getCalibration(); 
  }

  // how many settings does this device have
  virtual byte floatSettingsCount() 
  {
    return 1; 
  }
/*
  virtual byte integerSettingsCount() 
  {
    return 0; 
  }
*/

  // read settings from the device
  virtual double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return this->getCalibration();
    default:
      return -1.0f;
    }
  }
/*
  virtual int readIntegerSetting(byte index) 
  {
    return -1;
  }
*/

  // write settings to the device
  virtual bool writeFloatSetting(byte index, double val) 
  {
    switch (index) 
    {
    case 0:  
      this->setCalibration(val);
      return true;
    default:
      return false;
    }
  }
/*
  virtual bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }
*/

  // describe the device settings
  virtual const __FlashStringHelper *describeFloatSetting(byte index) 
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
  virtual const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    switch (index) 
    {
    default:
      return false;
    }
  }
*/

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration = this->getCalibration();
    settings.save(tempCalibration);
  }

  virtual void restoreSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    settings.restore(tempCalibration);
    this->setCalibration(tempCalibration);
  }
};


#endif
