#ifndef OSPTEMPERATUREINPUTCARD_H
#define OSPTEMPERATUREINPUTCARD_H

#include "ospCards.h"
#include "ospSettingsHelper.h"

class ospTemperatureInputCard : 
  public ospBaseInputCard 
{
private:


public:
  ospTemperatureInputCard() :
    ospBaseInputCard(),
    initialized(false),
    calibration(0.0f)
  { 
  }

  double calibration;
  bool initialized;

  const __FlashStringHelper *cardIdentifier() { return 0; };
  double readInput() { return NAN; }; 
  unsigned long requestInput() { return 0; }; // returns conversion time in milliseconds 
  byte floatSettingsCount() { return 0; }; 
  //byte integerSettingsCount() { return 0; }; 
  double readFloatSetting(byte index) { return -1.0f; };
  //int readIntegerSetting(byte index) { return -1; };
  bool writeFloatSetting(byte index, double val) { return false; }; 
  //bool writeIntegerSetting(byte index, int val) { return false; }; 
  const __FlashStringHelper *describeFloatSetting(byte index) { return 0; }; 
  //const __FlashStringHelper *describeIntegerSetting(byte index) { return 0; }; 
  void saveSettings(ospSettingsHelper& settings) {}; 
  void restoreSettings(ospSettingsHelper& settings) {}; 
};


#endif






