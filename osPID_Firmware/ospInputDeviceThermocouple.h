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

  // return the device identifier
  virtual const __FlashStringHelper *IODeviceIdentifier()
  {
    return F("Thermocouple K");
  }

  // read the device
  virtual double readInput() 
  {
    double val = thermocouple.readThermocouple(CELSIUS);
    if (val == FAULT_OPEN || val == FAULT_SHORT_GND || val == FAULT_SHORT_VCC)
      return NAN;
    return val + this->getCalibration();
  }
};


#endif
