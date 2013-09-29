jifndef OSPTEMPERATUREINPUTCARDTHERMISTOR_H
#define OSPTEMPERATUREINPUTCARDTHERMISTOR_H

#include "ospTemperatureInputCard.h"
#include "ospSettingsHelper.h"

class ospTemperatureInputCardThermistor : 
  public ospTemperatureInputCard
{
private:
  enum { thermistorPin = A0 };

  double THERMISTORNOMINAL;
  double BCOEFFICIENT;
  double TEMPERATURENOMINAL;
  double REFERENCE_RESISTANCE;


public:
  ospTemperatureInputCardThermistor() :
    ospTemperatureInputCard(),
    THERMISTORNOMINAL(10.0f),
    BCOEFFICIENT(1.0f),
    TEMPERATURENOMINAL(293.15f),
    REFERENCE_RESISTANCE(10.0f)
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
  // read the card
  double readInput() 
  {
    int voltage = analogRead(thermistorPin);
    return thermistorVoltageToTemperature(voltage) + calibration();
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
    return 5; 
  }
/*
  byte integerSettingsCount() 
  {
    return 0; 
  }
*/

  // read settings from the card
  double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return calibration();
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
    settings.save(THERMISTORNOMINAL);
    settings.save(BCOEFFICIENT);
    settings.save(TEMPERATURENOMINAL);
    settings.save(REFERENCE_RESISTANCE);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    settings.restore(tempCalibration);
    setCalibration(tempCalibration);
    settings.restore(THERMISTORNOMINAL);
    settings.restore(BCOEFFICIENT);
    settings.restore(TEMPERATURENOMINAL);
    settings.restore(REFERENCE_RESISTANCE);
  }
};



#endif
