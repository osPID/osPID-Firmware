// This header defines the base and utility classes for input and output cards
#ifndef OSPCARDS_H
#define OSPCARDS_H

class ospSettingsHelper;

// a base class for both input and output cards
class ospBaseCard {
public:
  ospBaseCard() { }
  
  // setup the card
  void initialize() { }
  
  // how many settings does this card have
  byte floatSettingsCount() { return 0; }
  byte integerSettingsCount() { return 0; }
  
  // read settings from the card
  float readFloatSetting(byte index) { return -1.0f; }
  int readIntegerSetting(byte index) { return -1; }
  
  // write settings to the card
  bool writeFloatSetting(byte index, float val) { return false; }
  bool writeIntegerSetting(byte index, int val) { return false; }
  
  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) { }
  void restoreSettings(ospSettingsHelper& settings) { }
};

class ospBaseInputCard : public ospBaseCard {
public:
  ospBaseInputCard() { ospBaseCard(); }
  
  float readInput() { return -1.0f; }
};

class ospBaseOutputCard : public ospBaseCard {
public:
  ospBaseOutputCard() { ospBaseCard(); }
  
  void setOutputPercent(float percentage) { }
};

#endif
