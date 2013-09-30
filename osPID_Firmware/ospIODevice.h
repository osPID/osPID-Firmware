// This header defines the base and utility classes for input and output devices  
#ifndef OSPIODEVICE_H
#define OSPIODEVICE_H

// classes defined with dummy methods to avoid overhead of pure virtual functions

class ospSettingsHelper;


// a base class for both input and output IO devices
class ospBaseIODevice 
{
public:
  ospBaseIODevice() { }

  // setup the IO device 
  virtual void initialize() {}; 

  // return an identifying name for this IO device, as a PSTR
  virtual const __FlashStringHelper *IODeviceIdentifier() { return NULL; }; 

  // how many settings does this IO device have
  virtual byte floatSettingsCount() { return 0xFF; }; 
  //virtual byte integerSettingsCount() { return 0xFF; }; 

  // read settings from the IO device
  virtual double readFloatSetting(byte index) { return -1.0f ;};
  //virtual int readIntegerSetting(byte index) { return -1; };

  // write settings to the IO device
  virtual bool writeFloatSetting(byte index, double val) { return false; };
  //virtual bool writeIntegerSetting(byte index, int val) { return false; };
  
  // return a text description of the N'th setting, as a PSTR
  // also returns the number of decimal places
  virtual const __FlashStringHelper *describeFloatSetting(byte index) { return NULL; };
  //virtual const __FlashStringHelper *describeIntegerSetting(byte index) { return NULL; };

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) {};
  virtual void restoreSettings(ospSettingsHelper& settings) {};
};


class ospBaseInputDevice  : public ospBaseIODevice 
{
public:
  ospBaseInputDevice()  :
    ospBaseIODevice() 
  {
  }

  virtual bool getInitializationStatus() { return false; };
  virtual void setInitializationStatus(bool newInitializationStatus) {};
  virtual double getCalibration() { return NAN; };
  virtual void setCalibration(double newCalibration) {};
  virtual unsigned long requestInput() { return -1; };
  virtual double readInput() { return NAN; };
};


class ospBaseOutputDevice : public ospBaseIODevice 
{
public:
  ospBaseOutputDevice() :
   ospBaseIODevice()
  {
  }
  
  virtual double getOutputWindowSeconds() { return NAN; };
  virtual void setOutputWindowSeconds(double newOutputWindowSeconds) {};
  virtual void setOutputPercent(double percentage) {};
};

#endif
