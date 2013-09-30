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
};


#endif
