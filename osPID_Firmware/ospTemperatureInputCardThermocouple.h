#ifndef OSPTEMPERATUREINPUTCARDTHERMOCOUPLE_H
#define OSPTEMPERATUREINPUTCARDTHERMOCOUPLE_H

#include "ospTemperatureInputCard.h"
#include "ospSettingsHelper.h"
#include "MAX31855_local.h"

class ospTemperatureInputCardThermocouple : 
  public ospTemperatureInputCard 
{
private:
  enum { thermocoupleSO = A0  };
  enum { thermocoupleCS = A1  };
  enum { thermocoupleCLK = A2 };

  MAX31855 thermocouple;

public:
  ospTemperatureInputCardThermocouple() :
    ospTemperatureInputCard(),
    thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO)
  { 
  }

  // setup the card
  void initialize() 
  {
    setInitialized(true);
  }

  // return the card identifier
  const __FlashStringHelper *cardIdentifier()
  {
    return F("Thermocouple K");
  }

  // read the card
  double readInput() 
  {
    double val = thermocouple.readThermocouple(CELSIUS);
    if (val == FAULT_OPEN || val == FAULT_SHORT_GND || val == FAULT_SHORT_VCC)
      val = NAN;
    return val + calibration();
  }

  // request input
  // returns conversion time in milliseconds
  unsigned long requestInput() 
  {
    return 0;
  }

  // how many settings does this card have
  byte floatSettingsCount() 
  {
    return 1; 
  }
  byte integerSettingsCount() 
  {
    return 0; 
  }

  // read settings from the card
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

  // write settings to the card
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

  // describe the card settings
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
