/* This file contains the setup() and loop() logic for the controller. */

#include <Arduino.h>
#include <LiquidCrystal.h>
#include "MyLiquidCrystal.h"
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include "ospAnalogButton.h"
#include "ospCardSimulator.h"
#include "ospDecimalValue.h"
#include "ospDigitalOutputStripboard.h"
#include "ospTemperatureInputStripboard.h"
#include "ospProfile.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MAIN"), __LINE__);

/*******************************************************************************
 * The stripboard PID Arduino shield uses firmware based on the osPID but
 * simplified hardware. Instead of swappable output cards there is a simple
 * 5V logical output that can drive an SSR. In place of output cards there
 * is a terminal block that can be used for a 1-wire bus or NTC thermistor,
 * and female pin headers that can interface a MAX31855 thermocouple amplifier
 * breakout board. Each of these inputs is supported by different
 * device drivers & libraries. The input in use is specified by a menu option.
 * This saves having to recompile the firmware when changing input sensors.
 *
 * Inputs
 * ======
 *    ospTemperatureInputStripboardV1_0:
 *    DS18B20+ 1-wire digital thermometer with data pin on A0, OR
 *    10K NTC thermistor with voltage divider input on pin A0, OR
 *    MAX31855KASA interface to type-K thermocouple on pins A0-A2.
 *
 * Output
 * ======
 *    ospDigitalOutputStripboardV1_0:
 *    1 SSR output on pin A3.
 *
 * For firmware development, there is also the ospCardSimulator which acts as both
 * the input and output cards and simulates the controller being attached to a
 * simple system.
 *******************************************************************************/

#undef USE_SIMULATOR
#ifndef USE_SIMULATOR
ospTemperatureInputStripboardV1_0 theInputCard;
ospDigitalOutputStripboardV1_0 theOutputCard;
#else
ospCardSimulator theInputCard
#define theOutputCard theInputCard
#endif

// the 7 character version tag is displayed in the startup tag and the Identify response
#define OSPID_VERSION_TAG "v3.0sps"

// we use the LiquidCrystal library to drive the LCD screen
MyLiquidCrystal theLCD(2, 3, 7, 6, 5, 4);

// our AnalogButton library provides debouncing and interpretation
// of the analog-multiplexed button channel
ospAnalogButton<A4, 0, 253, 454, 657> theButtonReader;

// an in-memory buffer that we use when receiving a profile over USB
ospProfile profileBuffer;

// the 0-based index of the active profile while a profile is executing
byte activeProfileIndex;
byte currentProfileStep;
boolean runningProfile = false;

// the name of this controller unit (can be queried and set over USB)
char controllerName[17] = { 
  'o', 's', 'P', 'I', 'D', ' ',
  'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r', '\0' };

// the gain coefficients of the PID controller
ospDecimalValue<3> PGain = { 
  2000 }
, IGain = { 
  500 }
, DGain = { 
  2000 };

// the direction flag for the PID controller
byte ctrlDirection = DIRECT;

// whether the controller is executing a PID law or just outputting a manual
// value
byte modeIndex = MANUAL;

// the 4 setpoints we can easily switch between
ospDecimalValue<1> setPoints[4] = { 
  { 
    250   }
  , { 
    750   }
  , { 
    1500   }
  , { 
    3000   } 
};

// the manually-commanded output value
ospDecimalValue<1> manualOutput = { 
  0 };

// the index of the selected setpoint
byte setpointIndex = 0;

// temporary values during the fixed-point conversion
ospDecimalValue<1> fakeSetpoint = { 
  750 }
, fakeInput = { 
  200 }
, fakeOutput = { 
  0 };

// temporary input calibration value
ospDecimalValue<1> DCalibration = { 
  0 };

// temporary value of output window length in seconds
ospDecimalValue<1> DWindow = { 
  50 };

// the variables to which the PID controller is bound
double setpoint = 75.0, input = NAN, output = 0.0, pidInput = 25.0;

// the hard trip limits
ospDecimalValue<1> lowerTripLimit = { 
  0 }
, upperTripLimit = { 
  2000 };
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
ospDecimalValue<1> aTuneStep = { 
  200 }
, aTuneNoise = { 
  10 };
int aTuneLookBack = 10;
PID_ATune aTune(&pidInput, &output);

// whether the autotuner is active
bool tuning = false;

// the actual PID controller
PID myPID(&pidInput, &output, &setpoint,double(PGain),double(IGain),double(DGain), DIRECT);

// timekeeping to schedule the various tasks in the main loop
unsigned long now, lcdTime, readInputTime;

// how often to step the PID loop, in milliseconds: it is impractical to set this
// to less than ~250 (i.e. faster than 4 Hz), since (a) the input card has up to 100 ms
// of latency, and (b) the controller needs time to handle the LCD, EEPROM, and serial
// I/O
enum { 
  PID_LOOP_SAMPLE_TIME = 1000 };

// initialize the controller: this is called by the Arduino runtime on bootup
void setup()
{
  lcdTime = 25;

  // set up the LCD
  theLCD.begin(16, 2);
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
  myPID.SetTunings(double(PGain), double(IGain), double(DGain));
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
      runningProfile = true;
      startCurrentProfileStep();
    }
    else
      recordProfileCompletion(); // we don't want to pick up again, so mark it completed
  }

  // kick things off by requesting sensor input
  now = millis();
  if (theInputCard.initialized)
    readInputTime = now + theInputCard.requestInput();

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
  PGain = (ospDecimalValue<3>){ 
    (int)(aTune.GetKp() * 1000.0)   };
  IGain = (ospDecimalValue<3>){ 
    (int)(aTune.GetKi() * 1000.0)   };
  DGain = (ospDecimalValue<3>){ 
    (int)(aTune.GetKd() * 1000.0)   };

  // set the PID controller to accept the new gain settings
  myPID.SetControllerDirection(DIRECT);
  myPID.SetMode(AUTOMATIC);

  if (PGain < (ospDecimalValue<3>){
    0  }
  )
  {
    // the auto-tuner found a negative gain sign: convert the coefficients
    // to positive with REVERSE controller action
    PGain = -PGain;
    IGain = -IGain;
    DGain = -DGain;
    myPID.SetControllerDirection(REVERSE);
    ctrlDirection = REVERSE;
  }
  else
  {
    ctrlDirection = DIRECT;
  }

  myPID.SetTunings(double(PGain), double(IGain), double(DGain));

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
    manualOutput = fakeOutput;

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

  // highest priority task is to update the output
  theOutputCard.setOutputPercent(output);

  // read input, if it is ready
  if (theInputCard.initialized && (now > readInputTime))
  {
    input = theInputCard.readInput();
    if (!isnan(input))
      pidInput = input;
  }

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

  // can't do much without input, so initializing input is next in line 
  if (theInputCard.initialized)
  {
    input = NAN;
    theInputCard.initialize();
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
      drawNotificationCursor('*');
      processSerialCommand();
      serialCommandLength = 0;
    }
  }
}


