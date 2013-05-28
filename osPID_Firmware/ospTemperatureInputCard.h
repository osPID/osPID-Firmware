#ifndef OSPTEMPERATUREINPUTCARD_H
#define OSPTEMPERATUREINPUTCARD_H

#include "ospCards.h"
#include "ospSettingsHelper.h"
#include "max6675_local.h"
#include "MAX31855_local.h"

template<typename TCType> class ospTemperatureInputCard : public ospBaseInputCard {
private:
  enum { thermistorPin = A6 };
  enum { thermocoupleCS = 10 };
  enum { thermocoupleSO = 12 };
  enum { thermocoupleCLK = 13 };

  enum { INPUT_THERMOCOUPLE = 0, INPUT_THERMISTOR = 1 };

  byte inputType;
  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;

  TCType thermocouple;

public:
  ospTemperatureInputCard() :
    ospBaseInputCard(),
    inputType(INPUT_THERMOCOUPLE),
    THERMISTORNOMINAL(10.0f),
    BCOEFFICIENT(1.0f),
    TEMPERATURENOMINAL(293.15f),
    REFERENCE_RESISTANCE(10.0f),
    thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO)
  { }

  // setup the card
  void initialize() { }

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

    return readThermocouple();
  }

  // how many settings does this card have
  byte floatSettingsCount() { return 4; }
  byte integerSettingsCount() { return 1; }

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
  bool writeSetting(byte index, int val) {
    if (index == 0 && (val == INPUT_THERMOCOUPLE || val == INPUT_THERMISTOR)) {
      inputType = val;
    } else if (index < 5)
      settingsBlock[index] = val;
    else
      return false;
    return true;
  }

  // describe the card settings
  const __FlashStringHelper * describeSetting(byte index, byte *decimals) {
    if (index < 3)
      *decimals = 0;
    else
      *decimals = 1;

    switch (index) {
    case 0:
      return F("Use the THERMOCOUPLE (0) or THERMISTOR (1) reader");
    case 1:
      return F("The thermistor nominal resistance (ohms)");
    case 2:
      return F("The reference resistor value (ohms)");
    case 3:
      return F("The thermistor B coefficient");
    case 4:
      return F("The thermistor reference temperature (Celsius)");
    default:
      return false;
    }
  }

  bool writeIntegerSetting(byte index, int val) {
    if (index == 0 && (val == INPUT_THERMOCOUPLE || val == INPUT_THERMISTOR)) {
      inputType = val;
      return true;
    }
    return false;
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

template<> double ospTemperatureInputCard<MAX6675>::readThermocouple() {
  return thermocouple.readCelsius();
}

template<> const __FlashStringHelper * ospTemperatureInputCard<MAX6675>::cardIdentifier() {
  return F("IN_TEMP_V1.10");
}

template<> double ospTemperatureInputCard<MAX31855>::readThermocouple() {
   double val = thermocouple.readThermocouple(CELSIUS);
 
   if (val == FAULT_OPEN || val == FAULT_SHORT_GND || val == FAULT_SHORT_VCC)
     val = NAN;

   return val;
}

template<> const __FlashStringHelper * ospTemperatureInputCard<MAX31855>::cardIdentifier() {
  return F("IN_TEMP_V1.20");
}

typedef ospTemperatureInputCard<MAX6675> ospTemperatureInputCardV1_10;
typedef ospTemperatureInputCard<MAX31855> ospTemperatureInputCardV1_20;

#endif

