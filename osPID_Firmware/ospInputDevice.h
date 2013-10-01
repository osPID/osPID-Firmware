#ifndef OSPINPUTDEVICE_H
#define OSPINPUTDEVICE_H

#include "ospIODevice.h"
#include "ospSettingsHelper.h"
#include "OneWire_local.h"
#include "DallasTemperature_local.h"
#include "MAX31855_local.h"

// class using crude switches instead of nice but bloaty methods

enum { INPUT_THERMISTOR = 0, INPUT_ONEWIRE, INPUT_THERMOCOUPLE };

byte inputType = INPUT_THERMISTOR;


class ospInputDevice : 
  public ospBaseInputDevice 
{
private:
  enum { thermistorPin = A0 };
  enum { oneWireBus = A0 };
  enum { thermocoupleSO = A0  };
  enum { thermocoupleCS = A1  };
  enum { thermocoupleCLK = A2 }; 

  bool initializationStatus;
  double calibration[3];
  
  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;

  OneWire oneWire;
  DallasTemperature oneWireDevice;
  DeviceAddress oneWireDeviceAddress;

  MAX31855 thermocouple;



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
  ospInputDevice() :
    ospBaseInputDevice(),
    initializationStatus(false),
    calibration({0.0f, 0.0f, 0.0f}),
    THERMISTORNOMINAL(10.0f),
    BCOEFFICIENT(1.0f),
    TEMPERATURENOMINAL(293.15f),
    REFERENCE_RESISTANCE(10.0f),
    oneWire(oneWireBus),
    oneWireDevice(&oneWire),
    thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO)
  { 
  }
  
  void initialize() 
  {
    if (inputType == INPUT_ONEWIRE)
    {
      oneWireDevice.begin();
      if (!oneWireDevice.getAddress(oneWireDeviceAddress, 0)) 
      {
        initializationStatus = false;
        return;
      }
      else 
      {
        oneWireDevice.setResolution(oneWireDeviceAddress, 12);
        oneWireDevice.setWaitForConversion(false);
      }
    }
    initializationStatus = true;
  }
  
  const __FlashStringHelper *IODeviceIdentifier()
  {
    switch (inputType)
    {
    case INPUT_THERMISTOR:
      return F("NTC thermistor");
    case INPUT_ONEWIRE:
      return F("DS18B20+");
    case INPUT_THERMOCOUPLE: 
      return F("K-type thermocouple");
    default:
      return NULL;
    }
  }
  
  // how many settings does this device have
  byte floatSettingsCount() 
  {
    return 7;
  }  

  // read settings from the device
  double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return calibration[INPUT_THERMISTOR];
    case 1:
      return calibration[INPUT_ONEWIRE];
    case 2:
      return calibration[INPUT_THERMOCOUPLE];
    case 3:
      return THERMISTORNOMINAL;
    case 4:
      return BCOEFFICIENT;
    case 5:
      return TEMPERATURENOMINAL;
    case 6:
      return REFERENCE_RESISTANCE;
    default:
      return NAN;
    }
  }
    
  // write settings to the device
  bool writeFloatSetting(byte index, double val) 
  {
    switch (index) 
    {
    case 0:  
      calibration[INPUT_THERMISTOR] = val;
      return true;
    case 1:  
      calibration[INPUT_ONEWIRE] = val;
      return true;
    case 2:  
      calibration[INPUT_THERMOCOUPLE] = val;
      return true;
    case 3:
      THERMISTORNOMINAL = val;
      return true;
    case 4:
      REFERENCE_RESISTANCE = val;
      return true;
    case 5:
      BCOEFFICIENT = val;
      return true;
    case 6:
      TEMPERATURENOMINAL = val;
      return true;
    default:
      return false;
    }
  }
  
  // describe the device settings
  const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    switch (index)
    {
    case 0:
      return F("Thermistor calibration value");
    case 1:
      return F("DS18B20+ calibration value");
    case 2:
      return F("Thermocouple calibration value");
    case 3:
      return F("Thermistor nominal resistance (Kohms)");
    case 4:
      return F("Reference resistor value (Kohms)");
    case 5:
      return F("Thermistor B coefficient");
    case 6:
      return F("Thermistor reference temperature (Celsius)");
    default:
      return NULL;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    settings.save(calibration[INPUT_THERMISTOR]);
    settings.save(calibration[INPUT_ONEWIRE]);
    settings.save(calibration[INPUT_THERMOCOUPLE]);
    settings.save(THERMISTORNOMINAL);
    settings.save(REFERENCE_RESISTANCE);
    settings.save(BCOEFFICIENT);
    settings.save(TEMPERATURENOMINAL);
    return;
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    settings.restore(calibration[INPUT_THERMISTOR]);
    settings.restore(calibration[INPUT_ONEWIRE]);
    settings.restore(calibration[INPUT_THERMOCOUPLE]);
    settings.restore(THERMISTORNOMINAL);
    settings.restore(REFERENCE_RESISTANCE);
    settings.restore(BCOEFFICIENT);
    settings.restore(TEMPERATURENOMINAL);
    return;
  }  

/*
  byte integerSettingsCount() 
  {
    return 0; 
  }

  int readIntegerSetting(byte index) 
  {
    return -1;
  }

  bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }

  const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    switch (index) 
    {
    default:
      return NULL;
    }
  }
*/

  // request input
  // returns conversion time in milliseconds
  unsigned long requestInput() 
  {
    if (inputType == INPUT_ONEWIRE)
    {
      oneWireDevice.requestTemperatures();
      return 750;
    }
    return 0;
  }

  double readInput()
  {
    double temperature;
    switch (inputType)
    {
    case INPUT_THERMISTOR:
      int voltage;
      voltage = analogRead(thermistorPin);
      temperature = thermistorVoltageToTemperature(voltage);
      break;
    case INPUT_ONEWIRE:
      temperature = oneWireDevice.getTempCByIndex(0);
      break;
    case INPUT_THERMOCOUPLE: 
      temperature = thermocouple.readThermocouple(CELSIUS);
      if ((temperature == FAULT_OPEN) || (temperature = FAULT_SHORT_GND) || (temperature == FAULT_SHORT_VCC))
      break;
    default:
      return NAN;
    }
#ifndef UNITS_FAHRENHEIT
    return temperature + this->getCalibration();
#else
    return (temperature * 1.8 + 32.0) + this->getCalibration();
#endif
  }
  
  // get initialization status
  bool getInitializationStatus()
  {
    return initializationStatus;
  }

  // set initialization status
  void setInitializationStatus(bool newInitializationStatus)
  {
    initializationStatus = newInitializationStatus;
  }

  // get calibration
  double getCalibration()
  {
    return calibration[inputType];
  }

  // set calibration
  void setCalibration(double newCalibration)
  {
    calibration[inputType] = newCalibration;
  }  
};


#endif
