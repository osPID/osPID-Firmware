/* This file contains the setup() and loop() logic for the controller. */

#include <LiquidCrystal.h>
#include <Arduino.h>
#include "AnalogButton_local.h"
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include "ospProfile.h"
#include "ospCardSimulator.h"
#include "ospTemperatureInputCard.h"
#include "ospDigitalOutputCard.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MAIN"), __LINE__);

/*******************************************************************************
* The osPID Kit comes with swappable IO cards which are supported by different
* device drivers & libraries. For the osPID firmware to correctly communicate with
* your configuration, you must specify the type of |theInputCard| and |theOutputCard|
* below.
*
* Please note that only 1 input card and 1 output card can be used at a time. 
* List of available IO cards:
*
* Input Cards
* ===========
* 1. ospTemperatureInputCardV1_10:
*    Temperature Basic V1.10 with 1 thermistor & 1 type-K thermocouple (MAX6675)
*    interface.
* 2. ospTemperatureInputCardV1_20:
*    Temperature Basic V1.20 with 1 thermistor & 1 type-K thermocouple 
*    (MAX31855KASA) interface.
* 3. (your subclass of ospBaseInputCard here):
*    Generic prototype card with input specified by user. Please add necessary
*    input processing in the section below.
*
* Output Cards
* ============
* 1. ospDigitalOutputCardV1_20: 
*    Output card with 1 SSR & 2 relay output.
* 2. ospDigitalOutputCardV1_50: 
*    Output card with 1 SSR & 2 relay output. Similar to V1.20 except LED mount
*    orientation.
* 3. (your subclass of ospBaseOutputCard here):
*    Generic prototype card with output specified by user. Please add necessary
*    output processing in the section below.
*
* For firmware development, there is also the ospCardSimulator which acts as both
* the input and output cards and simulates the controller being attached to a
* simple system.
*******************************************************************************/

#undef USE_SIMULATOR
#ifndef USE_SIMULATOR
ospTemperatureInputCardV1_20 theInputCard;
ospDigitalOutputCardV1_50 theOutputCard;
#else
ospCardSimulator theInputCard
#define theOutputCard theInputCard
#endif

// we use the LiquidCrystal library to drive the LCD screen
LiquidCrystal theLCD(A1, A0, 4, 7, 8, 9);

// our AnalogButton library provides debouncing and interpretation
// of the multiplexed theButtonReader
AnalogButton theButtonReader(A3, 0, 253, 454, 657);

// an in-memory buffer that we use when receiving a profile over USB
ospProfile profileBuffer;

// the 0-based index of the active profile while a profile is executing
byte activeProfileIndex;
byte currentProfileStep;
boolean runningProfile = false;

// the name of this controller unit (can be queried and set over USB)
char controllerName[17] = { 'o', 's', 'P', 'I', 'D', ' ',
       'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r', '\0' };

// the gain coefficients of the PID controller
double kp = 2, ki = 0.5, kd = 2;

// the direction flag for the PID controller
byte ctrlDirection = 0;

// whether the controller is executing a PID law or just outputting a manual
// value
byte modeIndex = 0;

// the 4 setpoints we can easily switch between
double setPoints[4] = { 25.0f, 75.0f, 150.0f, 300.0f };

// the index of the selected setpoint
byte setpointIndex = 0;

// the variables to which the PID controller is bound
double setpoint=250,input=250,output=50, pidInput=250;

// what to do on power-on
enum {
    POWERON_GOTO_MANUAL,
    POWERON_GOTO_SETPOINT,
    POWERON_RESUME_PROFILE
};

byte powerOnBehavior = POWERON_GOTO_MANUAL;

// the paremeters for the autotuner
double aTuneStep = 20, aTuneNoise = 1;
unsigned int aTuneLookBack = 10;
PID_ATune aTune(&pidInput, &output);

// whether the autotuner is active
bool tuning = false;

// the actual PID controller
PID myPID(&pidInput, &output, &setpoint,kp,ki,kd, DIRECT);

// timekeeping to schedule the various tasks in the main loop
unsigned long now, lcdTime, buttonTime, ioTime, serialTime;

void setup()
{
  lcdTime=10;
  buttonTime=1;
  ioTime=5;
  serialTime=6;

  // set up the LCD
  theLCD.begin(8, 2);

  // display a startup message
  theLCD.setCursor(0,0);
  theLCD.print(F(" osPID   "));
  theLCD.setCursor(0,1);
  theLCD.print(F(" v2.00bks"));

  now = millis();

  // set up the peripheral cards
  theInputCard.initialize();
  theOutputCard.initialize();

  // load the EEPROM settings
  setupEEPROM();

  // set up the serial interface
  setupSerial();

  delay(millis() < now + 1000 ? now + 1000 - millis() : 10);

  now = millis();

  // show the controller name?

  myPID.SetSampleTime(1000);
  myPID.SetOutputLimits(0, 100);
  myPID.SetTunings(kp, ki, kd);
  myPID.SetControllerDirection(ctrlDirection);
  myPID.SetMode(modeIndex);
}

byte heldButton;
unsigned long buttonPressTime;

// test the buttons and look for button presses or long-presses
void checkButtons()
{
  byte button = theButtonReader.get();
  byte executeButton = BUTTON_NONE;

  if (button != BUTTON_NONE)
  {
    if (heldButton == BUTTON_NONE)
      buttonPressTime = now + 125; // auto-repeat delay
    else if (heldButton == BUTTON_OK)
      ; // OK does long-press/short-press, not auto-repeat
    else if ((now - buttonPressTime) > 250)
    {
      // auto-repeat
      executeButton = button;
      buttonPressTime = now;
    }
    heldButton = button;
  }
  else if (heldButton != BUTTON_NONE)
  {
    if (now < buttonPressTime)
    {
      // the button hasn't triggered auto-repeat yet; execute it
      // on release
      executeButton = heldButton;
    }
    else if (heldButton == BUTTON_OK && (now - buttonPressTime) > 125)
    {
      // BUTTON_OK was held for at least 250 ms: execute a long-press
      okKeyLongPress();
    }
    heldButton = BUTTON_NONE;
  }

  switch (executeButton)
  {
  case BUTTON_NONE:
    break;

  case BUTTON_RETURN:
    backKeyPress();
    break;

  case BUTTON_UP:
    updownKeyPress(true);
    break;

  case BUTTON_DOWN:
    updownKeyPress(false);
    break;

  case BUTTON_OK:
    okKeyPress();
    break;
  }
}

void loop()
{
  now = millis();

  if (now >= buttonTime)
  {
    checkButtons();
    buttonTime += 50;
  }

  bool doIO = now >= ioTime;
  //read in the input
  if (doIO)
  { 
    ioTime+=250;
    input = theInputCard.readInput();
    if (!isnan(input))pidInput = input;
  }

  if (tuning)
  {
    byte val = (aTune.Runtime());

    if(val != 0)
    {
      tuning = false;
    }

    if (!tuning)
    {
      // FIXME: convert gain sign to PID action direction
      // We're done, set the tuning parameters
      kp = aTune.GetKp();
      ki = aTune.GetKi();
      kd = aTune.GetKd();
      myPID.SetTunings(kp, ki, kd);
      stopAutoTune();
      saveEEPROMSettings();
    }
  }
  else
  {
    // step the profile, if there is one running
    if (runningProfile)
      profileLoopIteration();

    // allow the PID to compute if necessary
    myPID.Compute();
  }

  if (doIO)
  {
    theOutputCard.setOutputPercent(output);
  }

  if (now > lcdTime)
  {
    drawMenu();
    lcdTime += 250;
  }
  if (millis() > serialTime)
  {
    //if(receivingProfile && (now-profReceiveStart)>profReceiveTimeout) receivingProfile = false;
    SerialReceive();
    SerialSend();
    serialTime += 500;
  }
}









