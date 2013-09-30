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

  virtual unsigned long requestInput() {};
  virtual double readInput() = 0;
};


#endif
