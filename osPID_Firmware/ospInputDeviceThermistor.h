#ifndef OSPINPUTDEVICETHERMISTOR_H
#define OSPINPUTDEVICETHERMISTOR_H

#include "ospInputDevice.h"
#include "ospSettingsHelper.h"

class ospInputDeviceThermistor : 
  public ospInputDevice
{
private:
  enum { thermistorPin = A0 };

  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;


public:
  ospInputDeviceThermistor() :
    ospInputDevice(),
    THERMISTORNOMINAL(10.0f),
    BCOEFFICIENT(1.0f),
    TEMPERATURENOMINAL(293.15f),
    REFERENCE_RESISTANCE(10.0f)
  { 
  }

  // return the device identifier
  virtual const __FlashStringHelper *IODeviceIdentifier()
  {
    return F("Thermistor NTC");
  }

private:
  // convert the thermistor voltage to a temperature
  double thermistorVoltageToTemperature(int voltage)
  {
    double R = REFERENCE_RESISTANCE / (1024.0/(double)voltage - 1);
    double steinhart;
    steinhart = R / THERMISTORNOMINAL;                // (R/Ro)
    steinhart = log(steinhart);                       // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                        // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                      // Invert
    steinhart -= 273.15;                              // convert to C
    return steinhart;
  }

public:
  // read the device
  virtual double readInput() 
  {
    int voltage = analogRead(thermistorPin);
    return thermistorVoltageToTemperature(voltage) + this->getCalibration();
  }

  // request input
  // returns conversion time in milliseconds
  virtual unsigned long requestInput() 
  {
    return 0;
  }

  // how many settings does this device have
  virtual byte floatSettingsCount() 
  {
    return 5; 
  }

  // read settings from the device
  virtual double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return getCalibration();
    case 1:
      return THERMISTORNOMINAL;
    case 2:
      return BCOEFFICIENT;
    case 3:
      return TEMPERATURENOMINAL;
    case 4:
      return REFERENCE_RESISTANCE;
    default:
      return -1.0f;
    }
  }

  // write settings to the device
  virtual bool writeFloatSetting(byte index, double val) 
  {
    switch (index) 
    {
    case 0:  
      this->setCalibration(val);
      return true;
    case 1:
      THERMISTORNOMINAL = val;
      return true;
    case 2:
      BCOEFFICIENT = val;
      return true;
    case 3:
      TEMPERATURENOMINAL = val;
      return true;
    case 4:
      REFERENCE_RESISTANCE = val;
      return true;
    default:
      return false;
    }
  }

  // describe the device settings
  virtual const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return F("Thermistor reference temperature (Celsius)");
    case 1:
      return F("Calibration temperature adjustment (Celsius)");
    case 2:
      return F("Thermistor nominal resistance (Kohms)");
    case 3:
      return F("Reference resistor value (Kohms)");
    case 4:
      return F("Thermistor B coefficient");
    default:
      return false;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration = this->getCalibration();
    settings.save(tempCalibration);
    settings.save(THERMISTORNOMINAL);
    settings.save(BCOEFFICIENT);
    settings.save(TEMPERATURENOMINAL);
    settings.save(REFERENCE_RESISTANCE);
  }

  virtual void restoreSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    settings.restore(tempCalibration);
    this->setCalibration(tempCalibration);
    settings.restore(THERMISTORNOMINAL);
    settings.restore(BCOEFFICIENT);
    settings.restore(TEMPERATURENOMINAL);
    settings.restore(REFERENCE_RESISTANCE);
  }
};



#endif
