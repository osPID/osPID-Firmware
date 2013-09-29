// This header defines the base and utility classes for input and output devices
#ifndef OSPIODEVICE_H
#define OSPIODEVICE_H

class ospSettingsHelper;

// a base class for both input and output IO devices
class ospBaseIODevice {
public:
  ospBaseIODevice() { }

  // setup the IO device 
  void initialize() { }

  // return an identifying name for this IO device, as a PSTR
  const __FlashStringHelper *IODeviceIdentifier() { return F(""); }

  // how many settings does this IO device have
  byte floatSettingsCount() { return 0; }
/*
  byte integerSettingsCount() { return 0; }
*/

  // read settings from the IO device
  double readFloatSetting(byte index) { return -1.0f; }
/*
  int readIntegerSetting(byte index) { return -1; }
*/

  // write settings to the IO device
  bool writeFloatSetting(byte index, float val) { return false; }
/*
  bool writeIntegerSetting(byte index, int val) { return false; }
*/

  // return a text description of the N'th setting, as a PSTR
  // also returns the number of decimal places
  const __FlashStringHelper *describeFloatSetting(byte index) { return 0; }
/*
  const __FlashStringHelper *describeIntegerSetting(byte index) { return 0; }
*/

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) { }
  void restoreSettings(ospSettingsHelper& settings) { }
};

class ospBaseInputDevice : public ospBaseIODevice
public:
  ospBaseInputDevice() { ospBaseIODevice(); }

  double readInput() { return -1.0f; }
};

class ospBaseOutputDevice : public ospBaseIODevice {
public:
  ospBaseOutputDevice() { ospBaseDevice(); }

  void setOutputPercent(double percentage) { }
};

#endif
