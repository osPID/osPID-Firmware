#ifndef OSPINPUTDEVICE_H
#define OSPINPUTDEVICE_H

#include "ospIODevice.h"
#include "ospSettingsHelper.h"

class ospInputDevice : 
  public ospBaseInputDevice 
{
private:

  bool initializationStatus;
  double calibration;


public:
  ospInputDevice() :
    ospBaseInputDevice(),
    initializationStatus(false),
    calibration(0.0)
  { 
  }
  
  virtual void initialize() 
  {
    this->setInitializationStatus(true);
  }
  
  virtual const __FlashStringHelper *IODeviceIdentifier() { return NULL; };
  
  // how many settings does this device have
  virtual byte floatSettingsCount() 
  {
    return 1; 
  }  

  // read settings from the device
  virtual double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return this->getCalibration();
    default:
      return -1.0f;
    }
  }
    
  // write settings to the device
  virtual bool writeFloatSetting(byte index, double val) 
  {
    switch (index) 
    {
    case 0:  
      this->setCalibration(val);
      return true;
    default:
      return false;
    }
  }
  
  // describe the device settings
  virtual const __FlashStringHelper *describeFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return F("Calibration temperature adjustment (Celsius)");
    default:
      return false;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  virtual void saveSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration = this->getCalibration();
    settings.save(tempCalibration);
  }

  virtual void restoreSettings(ospSettingsHelper& settings) 
  {
    double tempCalibration;
    settings.restore(tempCalibration);
    this->setCalibration(tempCalibration);
  }  

/*
  virtual byte integerSettingsCount() 
  {
    return 0; 
  }

  virtual int readIntegerSetting(byte index) 
  {
    return -1;
  }

  virtual bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }

  virtual const __FlashStringHelper *describeIntegerSetting(byte index) 
  {
    switch (index) 
    {
    default:
      return false;
    }
  }
*/

  // request input
  // returns conversion time in milliseconds
  virtual unsigned long requestInput() 
  {
    return 0;
  }

  virtual double readInput()
  {
    return NAN;
  }
  
  // get initialization status
  virtual bool getInitializationStatus()
  {
    return initializationStatus;
  }

  // set initialization status
  virtual void setInitializationStatus(bool newInitializationStatus)
  {
    initializationStatus = newInitializationStatus;
  }

  // get calibration
  virtual double getCalibration()
  {
    return calibration;
  }

  // set calibration
  virtual void setCalibration(double newCalibration)
  {
    calibration = newCalibration;
  }  
};


#endif
