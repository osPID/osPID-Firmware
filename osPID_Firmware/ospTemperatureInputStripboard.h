#ifndef OSPTEMPERATUREINPUTSTRIPBOARD_H
#define OSPTEMPERATUREINPUTSTRIPBOARD_H

#include "ospCards.h"
#include "ospSettingsHelper.h"
#include "MAX31855_local.h"
#include "OneWire_local.h"
#include "DallasTemperature_local.h"

template<typename TCType> class ospTemperatureInputCard : 
public ospBaseInputCard {
private:
  enum { 
    oneWireBus = A0         };
  enum { 
    thermistorPin = A0         };
  enum { 
    thermocoupleCS = A1         };
  enum { 
    thermocoupleSO = A0         };
  enum { 
    thermocoupleCLK = A2         };

  enum { 
    INPUT_THERMISTOR = 0, INPUT_THERMOCOUPLE = 1, INPUT_ONEWIRE = 2         };

  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;

  TCType thermocouple;

  OneWire oneWire;
  DallasTemperature ds18b20;
  DeviceAddress oneWireDeviceAddress;


public:
  ospTemperatureInputCard() :
  ospBaseInputCard(),
  inputType(INPUT_THERMISTOR),
  THERMISTORNOMINAL(10.0f),
  BCOEFFICIENT(1.0f),
  TEMPERATURENOMINAL(293.15f),
  REFERENCE_RESISTANCE(10.0f),
  thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO),
  oneWire(oneWireBus),
  ds18b20(&oneWire)
  { 
  }

  byte inputType;
  bool initialized;

  // setup the card
  void initialize() {
    if (inputType == INPUT_ONEWIRE) { 
      ds18b20.begin();
      if (!ds18b20.getAddress(oneWireDeviceAddress, 0)) {
        initialized=false;
      } 
      else {
        ds18b20.setResolution(oneWireDeviceAddress, 12);
        initialized=true;
      }
    }
  }

  /*
  // set input type
   void setInputType( int val ) {
   if ((val == INPUT_THERMISTOR) || (val == INPUT_THERMOCOUPLE) || (val == INPUT_ONEWIRE) ) {
   inputType = val;
   }
   }
   
   // get input type
   int setInputType(void) { 
   return inputType; 
   }
   */

  // return the card identifier
  const __FlashStringHelper * cardIdentifier();

private:
  // actually read the thermocouple
  double readThermocouple();

  // convert the thermistor voltage to a temperature
  double thermistorVoltageToTemperature(int voltage)
  {
    double R = REFERENCE_RESISTANCE / (1024.0/(double)voltage - 1);
    double steinhart;
    steinhart = R / THERMISTORNOMINAL;     // (R/Ro)
    steinhart = log(steinhart);                  // ln(R/Ro)
    steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
    steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
    steinhart = 1.0 / steinhart;                 // Invert
    steinhart -= 273.15;                         // convert to C

    return steinhart;
  }

public:
  // read the card
  double readInput() {
    if (inputType == INPUT_THERMISTOR) {
      int voltage = analogRead(thermistorPin);
      return thermistorVoltageToTemperature(voltage);
    }
    if (inputType == INPUT_THERMOCOUPLE) {
      return readThermocouple();
    }
    // obtain temperature from 1st device on 1-wire bus
    return ds18b20.getTempCByIndex(0); 
  }

  // request input
  // returns conversion time in milliseconds
  unsigned long requestInput() {
    if (inputType == INPUT_ONEWIRE) {
      ds18b20.requestTemperatures();
      return 750;
    }
    return 0;
  }

  // how many settings does this card have
  byte floatSettingsCount() { 
    return 4; 
  }
  byte integerSettingsCount() { 
    return 1; 
  }

  // read settings from the card
  double readFloatSetting(byte index) {
    switch (index) {
    case 0:
      return THERMISTORNOMINAL;
    case 1:
      return BCOEFFICIENT;
    case 2:
      return TEMPERATURENOMINAL;
    case 3:
      return REFERENCE_RESISTANCE;
    default:
      return -1.0f;
    }
  }

  int readIntegerSetting(byte index) {
    if (index == 0)
      return inputType;
    return -1;
  }

  // write settings to the card
  bool writeFloatSetting(byte index, double val) {
    switch (index) {
    case 0:  
      THERMISTORNOMINAL = val;
      return true;
    case 1:
      BCOEFFICIENT = val;
      return true;
    case 2:
      TEMPERATURENOMINAL = val;
      return true;
    case 3:
      REFERENCE_RESISTANCE = val;
      return true;
    default:
      return false;
    }
  }

  bool writeIntegerSetting(byte index, int val) {
    if (index == 0 && (val == INPUT_THERMOCOUPLE || val == INPUT_THERMISTOR || val == INPUT_ONEWIRE)) {
      inputType = val;
      return true;                                    
    } 
    return false;
  }

  // describe the card settings
  const __FlashStringHelper * describeSetting(byte index, byte *decimals) {
    if (index < 3)
      *decimals = 0;
    else
      *decimals = 1;

    switch (index) {
    case 0:
      return F("Use the THERMOCOUPLE (0) or THERMISTOR (1) or ONEWIRE (2) reader");
    case 1:
      return F("The thermistor nominal resistance (Kohms)");
    case 2:
      return F("The reference resistor value (Kohms)");
    case 3:
      return F("The thermistor B coefficient");
    case 4:
      return F("The thermistor reference temperature (Celsius)");
    default:
      return false;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) {
    settings.save(THERMISTORNOMINAL);
    settings.save(BCOEFFICIENT);
    settings.save(TEMPERATURENOMINAL);
    settings.save(REFERENCE_RESISTANCE);
    settings.save(inputType);
  }

  void restoreSettings(ospSettingsHelper& settings) {
    settings.restore(THERMISTORNOMINAL);
    settings.restore(BCOEFFICIENT);
    settings.restore(TEMPERATURENOMINAL);
    settings.restore(REFERENCE_RESISTANCE);
    settings.restore(inputType);
  }
};

template<> double ospTemperatureInputCard<MAX31855>::readThermocouple() {
  double val = thermocouple.readThermocouple(CELSIUS);

  if (val == FAULT_OPEN || val == FAULT_SHORT_GND || val == FAULT_SHORT_VCC)
    val = NAN;

  return val;
}

template<> const __FlashStringHelper * ospTemperatureInputCard<MAX31855>::cardIdentifier() {
  return F("IN_TEMP_V1.0");
}

typedef ospTemperatureInputCard<MAX31855> ospTemperatureInputStripboardV1_0;


#endif





