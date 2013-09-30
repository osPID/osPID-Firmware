#ifndef OSPSIMULATOR_H
#define OSPSIMULATOR_H

#include "ospIODevice.h"
#include "ospSettingsHelper.h"

// a "device" which simulates a simple plant including a proportional heating
// term, a thermal loss rate, and some measurement noise
class ospSimulator : public ospBaseInputDevice, public ospBaseOutputDevice 
{
private:
  double kpmodel, taup, theta[30];
  double input;

  static const double outputStart = 50.0f;
  static const double inputStart = 250.0f;

public:
  ospSimulator()
    : ospBaseInputDevice(), ospBaseOutputDevice()
  {
  }

  // setup the device
  void initialize() 
  {
    input = inputStart;
    for(int i = 0; i < 30; i++)
      theta[i] = outputStart;
    setInitializationStatus(true);
  }

  // return the device identifier
  const char *IODeviceIdentifier() { return "SIML"; }

  // how many settings does this device have
  byte floatSettingsCount() { return 2; }

  // read settings from the device
  double readFloatSetting(byte index) 
  {
    switch (index) 
    {
    case 0:
      return kpmodel;
    case 1:
      return taup;
    default:
      return -1.0f;
    }
  }

  // write settings to the device
  bool writeFloatSetting(byte index, double val) 
  {
    switch (index) 
    {
    case 0:
      kpmodel = val;
      return true;
    case 1:
      taup = val;
      return true;
    default:
      return false;
    }
  }

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    settings.save(kpmodel);
    settings.save(taup);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    settings.restore(kpmodel);
    settings.restore(taup);
  }

  // pretend to read an input from the input device
  double readInput() 
  {
    updateModel();
    return input;
  }

  // pretend to write a control signal to the output device
  void setOutputPercent(double percent) 
  {
    theta[29] = percent;
  }

private:
  void updateModel()
  {
    // Cycle the dead time
    for(byte i = 0; i < 30; i++)
    {
      theta[i] = theta[i+1];
    }
    // Compute the input
    input = (kpmodel / taup) * (theta[0] - outputStart) + (input - inputStart) * (1 - 1 / taup) + inputStart + ((double) random(-10, 10)) / 100;
  }
};



#endif
