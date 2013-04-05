#ifndef OSPDIGITALOUTPUTCARD_H
#define OSPDIGITALOUTPUTCARD_H

#include "ospCards.h"

class ospDigitalOutputCard : public ospBaseOutputCard {
private:
  enum { RelayPin = 5, SSRPin = 6 };
  enum { OUTPUT_RELAY = 0, OUTPUT_SSR = 1 };

  byte outputType;
  float outputWindowSeconds;
  unsigned long outputWindowMilliseconds;

public:
  ospDigitalOutputCard() 
    : ospBaseOutputCard(),
    outputType(OUTPUT_SSR),
    outputWindowSeconds(5.0),
    outputWindowMilliseconds(5000)
  { }

  void initialize() {
    pinMode(RelayPin, OUTPUT);
    pinMode(SSRPin, OUTPUT);
  }

  const char *cardIdentifier() { return "OID1"; }

  // how many settings does this card have
  byte floatSettingsCount() { return 1; }
  byte integerSettingsCount() { return 1; }

  // read settings from the card
  float readFloatSetting(byte index) {
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
  bool writeFloatSetting(byte index, float val) {
    if (index == 0) {
      outputWindowSeconds = val;
      outputWindowMilliseconds = round(outputWindowSeconds * 1000.0f);
      return true;
    }
    return false;
  }

  bool writeIntegerSetting(byte index, int val) {
    if (index == 0 && (val == OUTPUT_SSR || val == OUTPUT_RELAY)) {
      outputType = val;
      return true;
    }
    return false;
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

  void setOutputPercent(float percent) {
    unsigned long wind = millis() % outputWindowMilliseconds;
    unsigned long oVal = (unsigned long)(percent * (float)outputWindowMilliseconds / 100.0);

    if (outputType == OUTPUT_RELAY)
      digitalWrite(RelayPin, (oVal>wind) ? HIGH : LOW);
    else if(outputType == OUTPUT_SSR)
      digitalWrite(SSRPin, (oVal>wind) ? HIGH : LOW);
  }
};

typedef ospDigitalOutputCard ospDigitalOutputCardV1_20;
typedef ospDigitalOutputCard ospDigitalOutputCardV1_50;

#endif

