/* This file contains the setup() and loop() logic for the controller. */

#include <LiquidCrystal.h>
#include <Arduino.h>
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include "ospAnalogButton.h"
#include "ospCardSimulator.h"
#include "ospDigitalOutputCard.h"
#include "ospTemperatureInputCard.h"
#include "ospProfile.h"

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

// the 7 character version tag is displayed in the startup tag and the Identify response
#define OSPID_VERSION_TAG "v2.0bks"

// we use the LiquidCrystal library to drive the LCD screen
LiquidCrystal theLCD(A1, A0, 4, 7, 8, 9);

// our AnalogButton library provides debouncing and interpretation
// of the analog-multiplexed button channel
ospAnalogButton<A3, 0, 253, 454, 657> theButtonReader;

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
byte ctrlDirection = DIRECT;

// whether the controller is executing a PID law or just outputting a manual
// value
byte modeIndex = MANUAL;

// the 4 setpoints we can easily switch between
double setPoints[4] = { 25.0f, 75.0f, 150.0f, 300.0f };

// the index of the selected setpoint
byte setpointIndex = 0;

// the variables to which the PID controller is bound
double setpoint = 75.0, input = 30.0, output = 0.0, pidInput = 30.0, manualOutput = 0.0;

// the hard trip limits
double lowerTripLimit = 0.0, upperTripLimit = 200.0;
bool tripLimitsEnabled;
bool tripped;
bool tripAutoReset;

// what to do on power-on
enum {
    POWERON_DISABLE = 0,
    POWERON_CONTINUE_LOOP,
    POWERON_RESUME_PROFILE
};

byte powerOnBehavior = POWERON_CONTINUE_LOOP;

bool controllerIsBooting = true;

// the paremeters for the autotuner
double aTuneStep = 20, aTuneNoise = 1;
unsigned int aTuneLookBack = 10;
PID_ATune aTune(&pidInput, &output);

// whether the autotuner is active
bool tuning = false;

// the actual PID controller
PID myPID(&pidInput, &output, &setpoint,kp,ki,kd, DIRECT);

// timekeeping to schedule the various tasks in the main loop
unsigned long now, lcdTime;


// how often to step the PID loop, in milliseconds: it is impractical to set this
// to less than ~250 (i.e. faster than 4 Hz), since (a) the input card has up to 100 ms
// of latency, and (b) the controller needs time to handle the LCD, EEPROM, and serial
// I/O
enum { PID_LOOP_SAMPLE_TIME = 1000 };

// initialize the controller: this is called by the Arduino runtime on bootup
void setup()
{
  lcdTime = 25;

  // set up the LCD
  theLCD.begin(8, 2);
  drawStartupBanner();

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

  // configure the PID loop
  myPID.SetSampleTime(PID_LOOP_SAMPLE_TIME);
  myPID.SetOutputLimits(0, 100);
  myPID.SetTunings(kp, ki, kd);
  myPID.SetControllerDirection(ctrlDirection);

  if (powerOnBehavior == POWERON_DISABLE) {
    modeIndex = MANUAL;
    output = manualOutput;
  }
  myPID.SetMode(modeIndex);

  // finally, check whether we were interrupted in the middle of a profile
  if (profileWasInterrupted())
  {
    if (powerOnBehavior == POWERON_RESUME_PROFILE)
    {
      drawResumeProfileBanner();
      startCurrentProfileStep();
    }
    else
      recordProfileCompletion(); // we don't want to pick up again, so mark it completed
  }

  controllerIsBooting = false;
}

// Letting a button auto-repeat without redrawing the LCD in between leads to a
// poor user interface
bool lcdRedrawNeeded;

// keep track of which button is being held, and for how long
byte heldButton;
byte autoRepeatCount;
unsigned long autoRepeatTriggerTime;

// test the buttons and look for button presses or long-presses
static void checkButtons()
{
  byte button = theButtonReader.get();
  byte executeButton = BUTTON_NONE;

  enum {
    AUTOREPEAT_DELAY = 250,
    AUTOREPEAT_PERIOD = 350
  };

  if (button != BUTTON_NONE)
  {
    if (heldButton == BUTTON_NONE)
    {
      autoRepeatTriggerTime = now + AUTOREPEAT_DELAY;
    }
    else if (heldButton == BUTTON_OK)
    {
      // OK does long-press/short-press, not auto-repeat
    }
    else if (now > autoRepeatTriggerTime)
    {
      // don't auto-repeat until 100 ms after the redraw
      if (lcdRedrawNeeded)
      {
        autoRepeatTriggerTime = now + 150;
        return;
      }

      // auto-repeat
      executeButton = button;
      autoRepeatCount += 1;
      autoRepeatTriggerTime = now + AUTOREPEAT_PERIOD;
    }
    heldButton = button;
  }
  else if (heldButton != BUTTON_NONE)
  {
    if (heldButton == BUTTON_OK && (now > autoRepeatTriggerTime + (400 - AUTOREPEAT_DELAY)))
    {
      // BUTTON_OK was held for at least 400 ms: execute a long-press
      bool longPress = okKeyLongPress();

      if (!longPress)
      {
        // no long-press action defined, so fall back to a short-press
        executeButton = BUTTON_OK;
      }
    }
    else if (autoRepeatCount == 0)
    {
      // the button hasn't triggered auto-repeat yet; execute it on release
      executeButton = heldButton;
    }

    // else: the button was in auto-repeat, so don't execute it again on release
    heldButton = BUTTON_NONE;
    autoRepeatCount = 0;
  }

  if (executeButton == BUTTON_NONE)
    return;

  lcdRedrawNeeded = true;

  switch (executeButton)
  {
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

static void completeAutoTune()
{
  // We're done, set the tuning parameters
  kp = aTune.GetKp();
  ki = aTune.GetKi();
  kd = aTune.GetKd();

  // set the PID controller to accept the new gain settings
  myPID.SetControllerDirection(DIRECT);
  myPID.SetMode(AUTOMATIC);

  if (kp < 0)
  {
    // the auto-tuner found a negative gain sign: convert the coefficients
    // to positive with REVERSE controller action
    kp = -kp;
    ki = -ki;
    kd = -kd;
    myPID.SetControllerDirection(REVERSE);
    ctrlDirection = REVERSE;
  }
  else
  {
    ctrlDirection = DIRECT;
  }

  myPID.SetTunings(kp, ki, kd);

  // this will restore the user-requested PID controller mode
  stopAutoTune();

  markSettingsDirty();
}

bool settingsWritebackNeeded;
unsigned long settingsWritebackTime;

// record that the settings have changed, and need to be written to EEPROM
// as soon as they are done changing
static void markSettingsDirty()
{
  // capture any possible changes to the output value if we're in MANUAL mode
  if (modeIndex == MANUAL && !tuning && !tripped)
    manualOutput = output;

  settingsWritebackNeeded = true;

  // wait until nothing has changed for 5s before writing to EEPROM
  // this reduces EEPROM wear by not writing every time a digit is changed
  settingsWritebackTime = now + 5000;
}

// This is the Arduino main loop.
//
// There are two goals this loop must balance: the highest priority is
// that the PID loop be executed reliably and on-time; the other goal is that
// the screen, buttons, and serial interfaces all be responsive. However,
// since things like redrawing the LCD may take tens of milliseconds -- and responding
// to serial commands can take 100s of milliseconds at low bit rates -- a certain
// measure of cleverness is required.
//
// Alongside the real-time task of the PID loop, there are 6 other tasks which may
// need to be performed:
// 1. handling a button press
// 2. executing a step of the auto-tuner
// 3. executing a step of a profile
// 4. redrawing the LCD
// 5. saving settings to EEPROM
// 6. processing a serial-port command
//
// Characters from the serial port are received asynchronously: it is only the
// command _processing_ which needs to be scheduled.

// whether loop() is permitted to do LCD, EEPROM, or serial I/O: this is set
// to false when loop() is being re-entered during some slow operation
bool blockSlowOperations;

void realtimeLoop()
{
  if (controllerIsBooting)
    return;

  blockSlowOperations = true;
  loop();
  blockSlowOperations = false;
}

// we accumulate characters for a single serial command in this buffer
char serialCommandBuffer[33];
byte serialCommandLength;

void loop()
{
  // first up is the realtime part of the loop, which is not allowed to perform
  // EEPROM writes or serial I/O
  now = millis();

  // read in the input
  input = theInputCard.readInput();

  if (!isnan(input))
    pidInput = input;

  if (tuning)
  {
    byte val = aTune.Runtime();

    if (val != 0)
    {
      tuning = false;
      completeAutoTune();
    }
  }
  else
  {
    // step the profile, if there is one running
    // this may call ospSettingsHelper::eepromClearBits(), but not
    // ospSettingsHelper::eepromWrite()
    if (runningProfile)
      profileLoopIteration();

    // update the PID
    myPID.Compute();
  }

  // after the PID has updated, check the trip limits
  if (tripLimitsEnabled)
  {
    if (tripAutoReset)
      tripped = false;

    if (isnan(input) || input < lowerTripLimit || input > upperTripLimit || tripped)
    {
      output = 0;
      tripped = true;
    }
  }

  theOutputCard.setOutputPercent(output);

  // after the realtime part comes the slow operations, which may re-enter
  // the realtime part of the loop but not the slow part
  if (blockSlowOperations)
    return;

  // update the time after each major operation;
  now = millis();

  // we want to monitor the buttons as often as possible
  checkButtons();

  // we try to keep an LCD frame rate of 4 Hz, plus refreshing as soon as
  // a button is pressed
  now = millis();
  if (now > lcdTime || lcdRedrawNeeded)
  {
    drawMenu();
    lcdRedrawNeeded = false;
    lcdTime += 250;
  }

  now = millis();
  if (settingsWritebackNeeded && now > settingsWritebackTime) {
    // clear settingsWritebackNeeded first, so that it gets re-armed if the
    // realtime loop calls markSettingsDirty()
    settingsWritebackNeeded = false;

    // display a '$' instead of the cursor to show that we're saving to EEPROM
    drawNotificationCursor('$');
    saveEEPROMSettings();
  }

  // accept any pending characters from the serial buffer
  byte avail = Serial.available();
  while (avail--)
  {
    char ch = Serial.read();
    if (serialCommandLength < 32)
    {
      // throw away excess characters
      serialCommandBuffer[serialCommandLength++] = ch;
    }

    if (ch == '\n')
    {
      // a complete command has been received
      serialCommandBuffer[serialCommandLength] = '\0';
      processSerialCommand();
      serialCommandLength = 0;
      drawNotificationCursor('*');
    }
  }
}

