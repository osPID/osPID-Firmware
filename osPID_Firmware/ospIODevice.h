// This header defines the base and utility classes for input and output devices
#ifndef OSPIODEVICE_H
#define OSPIODEVICE_H

class ospSettingsHelper;

/*
// a base class for both input and output IO devices
class ospBaseIODevice 
{
public:
  ospBaseIODevice() { }

  // setup the IO device 
  virtual void initialize() = 0; 

  // return an identifying name for this IO device, as a PSTR
  virtual const __FlashStringHelper *IODeviceIdentifier() = 0; 

  // how many settings does this IO device have
  virtual byte floatSettingsCount() = 0; 
  virtual byte integerSettingsCount() = 0; 

  // read settings from the IO device
  virtual double readFloatSetting(byte index) = 0;
  //virtual int readIntegerSetting(byte index) = 0;

  // write settings to the IO device
  virtual bool writeFloatSetting(byte index, double val) = 0;
  //virtual bool writeIntegerSetting(byte index, int val) = 0;

  // return a text description of the N'th setting, as a PSTR
  // also returns the number of decimal places
  virtual const __FlashStringHelper *describeFloatSetting(byte index) = 0;
  //virtual const __FlashStringHelper *describeIntegerSetting(byte index) = 0;

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) = 0;
  virtual void restoreSettings(ospSettingsHelper& settings) = 0;
};
*/


class ospBaseInputDevice /* : public ospBaseIODevice */
{
public:
  ospBaseInputDevice() /* :
    ospBaseIODevice()*/ 
  {
  }

  virtual void initialize() = 0;
  virtual const __FlashStringHelper *IODeviceIdentifier() = 0; 
  virtual byte floatSettingsCount() = 0; 
  //virtual byte integerSettingsCount() = 0;
  virtual double readFloatSetting(byte index) = 0;
  //virtual int readIntegerSetting(byte index) = 0;
  virtual bool writeFloatSetting(byte index, double val) = 0;
  //virtual bool writeIntegerSetting(byte index, int val) = 0;
  virtual const __FlashStringHelper *describeFloatSetting(byte index) = 0;
  //virtual const __FlashStringHelper *describeIntegerSetting(byte index) = 0;
  virtual void saveSettings(ospSettingsHelper& settings) = 0;
  virtual void restoreSettings(ospSettingsHelper& settings) = 0;

  virtual bool getInitializationStatus() = 0;
  virtual void setInitializationStatus(bool newInitializationStatus) = 0;
  virtual double getCalibration() = 0;
  virtual void setCalibration(double newCalibration) = 0;
  virtual unsigned long requestInput() = 0;
  virtual double readInput() = 0;
};


class ospBaseOutputDevice /* : public ospBaseIODevice */ 
{
public:
  ospBaseOutputDevice() /* :
   ospBaseIODevice() */
  {
  }

  virtual void initialize() = 0;
  virtual const __FlashStringHelper *IODeviceIdentifier() = 0; 
  virtual byte floatSettingsCount() = 0; 
  //virtual byte integerSettingsCount() = 0;
  virtual double readFloatSetting(byte index) = 0;
  //virtual int readIntegerSetting(byte index) = 0;
  virtual bool writeFloatSetting(byte index, double val) = 0;
  //virtual bool writeIntegerSetting(byte index, int val) = 0;
  virtual const __FlashStringHelper *describeFloatSetting(byte index) = 0;
  //virtual const __FlashStringHelper *describeIntegerSetting(byte index) = 0;
  virtual void saveSettings(ospSettingsHelper& settings) = 0;
  virtual void restoreSettings(ospSettingsHelper& settings) = 0;

  virtual double getOutputWindowSeconds() = 0;
  virtual void setOutputWindowSeconds(double newOutputWindowSeconds) = 0; 
  virtual void setOutputPercent(double percentage) = 0; 
};

#endif
