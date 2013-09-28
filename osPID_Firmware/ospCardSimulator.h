#ifndef OSPCARDSIMULATOR_H
#define OSPCARDSIMULATOR_H

#include "ospCards.h"
#include "ospSettingsHelper.h"

// a "card" which simulates a simple plant including a proportional heating
// term, a thermal loss rate, and some measurement noise
class ospCardSimulator : public ospBaseInputCard, public ospBaseOutputCard 
{
private:
  double kpmodel, taup, theta[30];
  double input;

  static const double outputStart = 50.0f;
  static const double inputStart = 250.0f;

public:
  ospCardSimulator()
    : ospBaseInputCard()
    , ospBaseOutputCard()
  {
  }

  // setup the card
  void initialize() 
  {
    input = inputStart;

    for(int i = 0; i < 30; i++)
      theta[i] = outputStart;
  }

  // return the card identifier
  const char *cardIdentifier() { return "SIML"; }

  // how many settings does this card have
  byte floatSettingsCount() { return 2; }
/*
  byte integerSettingsCount() { return 0; }
*/

  // read settings from the card
  double readFloatSetting(byte index) 
  {
    switch (index) {
    case 0:
      return calibration;
    case 1:  
      return kpmodel;
    case 2:
      return taup;
    default:
      return -1.0f;
    }
  }
/*
  int readIntegerSetting(byte index) 
  {
    return -1;
  }
*/

  // write settings to the card
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
/*
  bool writeIntegerSetting(byte index, int val) 
  {
    return false;
  }
*/

  // save and restore settings to/from EEPROM using the settings helper
  void saveSettings(ospSettingsHelper& settings) 
  {
    settings.save(calibration);
    settings.save(kpmodel);
    settings.save(taup);
  }

  void restoreSettings(ospSettingsHelper& settings) 
  {
    settings.restore(calibration);
    settings.restore(kpmodel);
    settings.restore(taup);
  }

  // pretend to read an input from the input card
  double readInput() 
  {
    updateModel();
    return input;
  }

  // pretend to write a control signal to the output card
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
    input = (kpmodel / taup) *(theta[0]-outputStart) + (input-inputStart)*(1-1/taup)+inputStart + ((double)random(-10,10))/100;
  }
};

#endif

