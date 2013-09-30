#ifndef OSPINPUTDEVICE_H
#define OSPINPUTDEVICE_H

#include "ospIODevice.h"
#include "ospSettingsHelper.h"
#include "OneWire_local.h"
#include "DallasTemperature_local.h"
#include "MAX31855_local.h"

// class using crude switches instead of nice but bloaty virtual methods

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

  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;

  OneWire oneWire;
  DallasTemperature oneWireDevice;
  DeviceAddress oneWireDeviceAddress;

  MAX31855 thermocouple;
  
  double kpmodel, taup, theta[30];
  double input;

  static const double outputStart = 50.0f;
  static const double inputStart = 250.0f;

  bool initializationStatus;
  double calibration;


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
    calibration(0.0),
    THERMISTORNOMINAL(10.0f),
    BCOEFFICIENT(1.0f),
    TEMPERATURENOMINAL(293.15f),
    REFERENCE_RESISTANCE(10.0f),
    oneWire(oneWireBus),
    oneWireDevice(&oneWire),
    thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO)
  { 
  }
  
  virtual void initialize() 
  {
    if (inputType == INPUT_ONEWIRE)
    {
      oneWireDevice.begin();
      if (!oneWireDevice.getAddress(oneWireDeviceAddress, 0)) 
      {
        this->setInitializationStatus(false);
        return;
      }
      else 
      {
        oneWireDevice.setResolution(oneWireDeviceAddress, 12);
        oneWireDevice.setWaitForConversion(false);
      }
    }
    this->setInitializationStatus(true);
  }
  
  virtual const __FlashStringHelper *IODeviceIdentifier()
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
  virtual byte floatSettingsCount() 
  {
    switch (inputType)
    {
      return 5;
    }
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
      return NAN;
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
      return NULL;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    tempCalibration = this->getCalibration();
    settings.save(tempCalibration);
    settings.save(THERMISTORNOMINAL);
    settings.save(BCOEFFICIENT);
    settings.save(TEMPERATURENOMINAL);
    settings.save(REFERENCE_RESISTANCE);
    return;
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
    return;
  }  

/*
  virtual byte integerSettingsCount() 
  {
    return 0; 
  }

  virtual int readIntegerSetting(byte index) 
  {
    return -1;
  }

  virtual bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }

  virtual const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    switch (index) 
    {
    default:
      return false;
    }
  }
*/

  // request input
  // returns conversion time in milliseconds
  virtual unsigned long requestInput() 
  {
    if (inputType == INPUT_ONEWIRE)
    {
      oneWireDevice.requestTemperatures();
      return 750;
    }
    return 0;
  }

  virtual double readInput()
  {
    switch (inputType)
    {
    case INPUT_THERMISTOR:
      int voltage;
      voltage = analogRead(thermistorPin);
      return thermistorVoltageToTemperature(voltage) + this->getCalibration();
    case INPUT_ONEWIRE:
      return oneWireDevice.getTempCByIndex(0) + this->getCalibration();
    case INPUT_THERMOCOUPLE: 
      double val;
      val = thermocouple.readThermocouple(CELSIUS);
      if ((val == FAULT_OPEN) || (val == FAULT_SHORT_GND) || (val == FAULT_SHORT_VCC))
        return NAN;
      return val + this->getCalibration();
    default:
      return NAN;
    }
  }
  
  // get initialization status
  virtual bool getInitializationStatus()
  {
    return initializationStatus;
  }

  // set initialization status
  virtual void setInitializationStatus(bool newInitializationStatus)
  {
    initializationStatus = newInitializationStatus;
  }

  // get calibration
  virtual double getCalibration()
  {
    return calibration;
  }

  // set calibration
  virtual void setCalibration(double newCalibration)
  {
    calibration = newCalibration;
  }  
};


#endif
