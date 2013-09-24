#ifndef OSPDIGITALOUTPUTSTRIPBOARD_H
#define OSPDIGITALOUTPUTSTRIPBOARD_H

#include "ospCards.h"

class ospDigitalOutputCard : public ospBaseOutputCard {
private:
  enum { SSRPin = A3 };
  enum { OUTPUT_SSR = 1 };

  byte outputType;
  double outputWindowSeconds;
  unsigned long outputWindowMilliseconds;

public:
  ospDigitalOutputCard() 
    : ospBaseOutputCard(),
    outputType(OUTPUT_SSR),
    outputWindowSeconds(5.0),
    outputWindowMilliseconds(5000)
  { }

  void initialize() {
    pinMode(SSRPin, OUTPUT);
  }

  const __FlashStringHelper *cardIdentifier() { return F("OUT_DIGITAL"); }

  // how many settings does this card have
  byte floatSettingsCount() { return 1; }
  byte integerSettingsCount() { return 1; }

  // read settings from the card
  double readFloatSetting(byte index) {
    if (index == 0)
      return outputWindowSeconds;
    return -1.0f;
  }

  int readIntegerSetting(byte index) {
    if (index == 0)
      return outputType;
    return -1;
  }

  // write settings to the card
  bool writeFloatSetting(byte index, double val) {
    if (index == 0) {
      outputWindowSeconds = val;
      outputWindowMilliseconds = round(outputWindowSeconds * 1000.0f);
      return true;
    }
    return false;
  }

  // describe the available settings
  const __FlashStringHelper *describeSetting(byte index, byte *decimals) {
    *decimals = 0;
    if (index == 0) {
      return F("Output type = SSR (1)");
    } else if (index == 1) {
      return F("Output PWM window size in milliseconds");
    } else if (index == 2) {
      return F("Minimum time between PWM edges in milliseconds");
    } else
      return 0;
  }

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) {
    settings.save(outputWindowMilliseconds);
    settings.save(outputType);
  }

  void restoreSettings(ospSettingsHelper& settings) {
    settings.restore(outputWindowMilliseconds);
    settings.restore(outputType);
  }

  void setOutputPercent(double percent) {
    unsigned long wind = millis() % outputWindowMilliseconds;
    unsigned long oVal = (unsigned long)(percent * 0.01 * (double)outputWindowMilliseconds);

    if (outputType == OUTPUT_RELAY)
      digitalWrite(RelayPin, (oVal>wind) ? HIGH : LOW);
    else if(outputType == OUTPUT_SSR)
      digitalWrite(SSRPin, (oVal>wind) ? HIGH : LOW);
  }
};

typedef ospDigitalOutputCard ospDigitalOutputCardV1_20;
typedef ospDigitalOutputCard ospDigitalOutputCardV1_50;

#endif

