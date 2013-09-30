/* This file contains the setup() and loop() logic for the controller. */

#include <Arduino.h>
#include <LiquidCrystal.h>
#include <avr/pgmspace.h>
#include "defines.h"
#include "MyLiquidCrystal.h"
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include "ospAnalogButton.h"
#include "ospDecimalValue.h"
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
 *    DS18B20+ 1-wire digital thermometer with data pin on A0, OR
 *    10K NTC thermistor with voltage divider input on pin A0, OR
 *    MAX31855KASA interface to type-K thermocouple on pins A0-A2.
 *
 * Output
 * ======
 *    1 SSR output on pin A3.
 *
 * For firmware development, there is also the ospSimulator which acts as both
 * the input and output device and simulates the controller being attached to a
 * simple system.
 *******************************************************************************/


#ifndef USE_SIMULATOR
#include "ospOutputDeviceSsr.h"
#include "ospInputDeviceOneWire.h"
#include "ospInputDeviceThermocouple.h"
#include "ospInputDeviceThermistor.h"
ospInputDeviceThermistor thermistor;
ospInputDeviceThermocouple thermocouple;
ospInputDeviceOneWire ds18b20;
enum { numInputDevices = 3 };
ospInputDevice *inputDevice[numInputDevices] = { &thermistor, &ds18b20, &thermocouple };
enum { INPUT_THERMISTOR = 0, INPUT_ONEWIRE = 1, INPUT_THERMOCOUPLE = 2 };
byte inputType = INPUT_THERMISTOR;
ospOutputDeviceSsr ssr;
enum { numOutputDevices = 1 };
enum { OUTPUT_SSR = 0 };
byte outputType = OUTPUT_SSR;
ospBaseOutputDevice *outputDevice[numOutputDevices] = { &ssr };
#else
#include "ospSimulator.h"
ospSimulator simulator;
enum { numInputDevices = 1 };
ospInputDevice *inputDevice[numInputDevices] = { &simulator };
enum { SIMULATOR = 0 };
byte inputType = SIMULATOR;
enum { numOutputDevices = 1 };
byte outputType = SIMULATOR;
ospOutputDevice *outputDevice[numOutputDevices] = { &simulator };
#define theOutputDevice theInputDevice
#endif


ospInputDevice       *theInputDevice  = inputDevice[inputType];
ospBaseOutputDevice  *theOutputDevice = outputDevice[outputType];



// we use the LiquidCrystal library to drive the LCD screen
MyLiquidCrystal theLCD(2, 3, 7, 6, 5, 4);

// our AnalogButton library provides debouncing and interpretation
// of the analog-multiplexed button channel
ospAnalogButton<A4, 100, 253, 454, 657> theButtonReader;

// Pin assignment for buzzer
enum { buzzerPin = A5 };

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
ospDecimalValue<3> PGain = { 2000 } , IGain = { 500 } , DGain = { 2000 };

// the direction flag for the PID controller
byte ctrlDirection = DIRECT;

// whether the controller is executing a PID law or just outputting a manual
// value
byte modeIndex = MANUAL;

// the 4 setpoints we can easily switch between
// units may be Celsius or Fahrenheit
ospDecimalValue<1> setPoints[4] = { { 250 }, { 750 }, { 1500 }, { 3000 } };

// the index of the selected setpoint
byte setpointIndex = 0;

// the manually-commanded output value
ospDecimalValue<1> manualOutput = { 0 };

// temporary fixed point decimal values for display and data entry
// units may be Celsius or Fahrenheit
ospDecimalValue<1> displaySetpoint = { 250 }, displayInput = { -19999 }, displayCalibration = { 0 };
ospDecimalValue<1> displayOutput = { 0 }; // percentile

// flag to display menu options in Fahrenheit
bool displayCelsius = true;

// flag to change units of display values between Celsius and Fahrenheit
bool changeUnitsFlag = false;

// the most recent measured input value
double input = NAN; 

// the variables to which the PID controller is bound
// temperatures all in Celsius
double activeSetPoint = 25.0;

// last good input value
double lastGoodInput = 25.0;

// the output duty cycle calculated by PID controller
double output = 0.0;   

// temporary value of output window length in seconds
ospDecimalValue<1> displayWindow = { 50 };

// the hard trip limits
// units may be Fahrenheit
ospDecimalValue<1> lowerTripLimit = { 0 } , upperTripLimit = { 2000 };
bool tripLimitsEnabled;
bool tripped;
bool tripAutoReset;

// what to do on power-on
enum 
{
  POWERON_DISABLE = 0,
  POWERON_CONTINUE_LOOP,
  POWERON_RESUME_PROFILE
};

byte powerOnBehavior = POWERON_CONTINUE_LOOP;

bool controllerIsBooting = true;

// the parameters for the autotuner
ospDecimalValue<1> aTuneStep = { 200 } , aTuneNoise = { 10 }; int aTuneLookBack = 10;
PID_ATune aTune(&lastGoodInput, &output);

// whether the autotuner is active
bool tuning = false;

// the actual PID controller
PID myPID(&lastGoodInput, &output, &activeSetPoint, double(PGain), double(IGain), double(DGain), DIRECT);

// timekeeping to schedule the various tasks in the main loop
unsigned long now, lcdTime, readInputTime;

// how often to step the PID loop, in milliseconds: it is impractical to set this
// to less than ~1000 (i.e. faster than 1 Hz), since (a) the input has up to 750 ms
// of latency, and (b) the controller needs time to handle the LCD, EEPROM, and serial
// I/O
enum { PID_LOOP_SAMPLE_TIME = 1000 };

PROGMEM long serialSpeedTable[7] = { 9600, 14400, 19200, 28800, 38400, 57600, 115200 };

char hex(byte b)
{
  return ((b < 10) ? (char) ('0' + b) : (char) ('A' - 10 + b));
}




// Temperature conversion functions

ospDecimalValue<1> convertCtoF(ospDecimalValue<1> t)
{
  t = (t * (ospDecimalValue<1>){18}).rescale<1>();
  t = t + (ospDecimalValue<1>){320};
  return t;
}

double convertCtoF(double t)
{
  return (t * 1.8 + 32.0);
}

ospDecimalValue<1> convertFtoC(ospDecimalValue<1> t)
{
  return ((t - (ospDecimalValue<1>){320}) / (ospDecimalValue<1>){18}).rescale<1>();
}

double convertFtoC(double t)
{
  return ((t - 32.0 ) / 1.8);
}

double celsius(double t)
{
  return (displayCelsius ? t : convertFtoC(t));
}

double displayUnits(double t)
{
  return (displayCelsius ? t : convertCtoF(t));
}





// initialize the controller: this is called by the Arduino runtime on bootup
void setup()
{
  lcdTime = 25;

  // set up the LCD,show controller name
  theLCD.begin(16, 2);
  //drawStartupBanner();

  now = millis();

  // load the EEPROM settings
  //clearEEPROM();
  setupEEPROM();
  //saveEEPROMSettings();
  
  // set up the peripheral devices
  theInputDevice->initialize();
  theOutputDevice->initialize();

  // set up the serial interface
/* FIXME commented out temporarily to save space
 *
 *
  setupSerial();
 *
 *
 */

  delay((millis() < now + 1000) ? (now + 1000 - millis()) : 10);

  now = millis();

  // configure the PID loop
  myPID.SetSampleTime(PID_LOOP_SAMPLE_TIME);
  myPID.SetOutputLimits(0, 100);
  myPID.SetTunings(double(PGain), double(IGain), double(DGain));
  myPID.SetControllerDirection(ctrlDirection);

  if (powerOnBehavior == POWERON_DISABLE) 
  {
    modeIndex = MANUAL;
    output = double(manualOutput);
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
  if (theInputDevice->getInitializationStatus())
    readInputTime = now + theInputDevice->requestInput();

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

  enum 
  {
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
  PGain = makeDecimal<3>(aTune.GetKp());
  IGain = makeDecimal<3>(aTune.GetKi());
  DGain = makeDecimal<3>(aTune.GetKd());

  // set the PID controller to accept the new gain settings
  myPID.SetControllerDirection(DIRECT);
  myPID.SetMode(AUTOMATIC);

  if (PGain < (ospDecimalValue<3>){0})
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
    manualOutput = displayOutput;

  // capture any changes to the setpoint
  activeSetPoint = celsius(double(setPoints[setpointIndex]));

  // capture any changes to the output window length
  theOutputDevice->setOutputWindowSeconds(double(displayWindow));
  
  // capture any changes to the calibration value
  theInputDevice->setCalibration(double(displayCalibration) / (displayCelsius ? 1.0 : 1.8));

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
  theOutputDevice->setOutputPercent(output);

  // read input, if it is ready
  if (theInputDevice->getInitializationStatus() && (now > readInputTime))
  {
    input = theInputDevice->readInput();
    if (!isnan(input))
    {
      lastGoodInput = input;
      displayInput = makeDecimal<1>(displayCelsius ? input : convertCtoF(input));
    }
    else
    {
      displayInput = (ospDecimalValue<1>){-19999}; // display Err
    }
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
    {
      tripped = false;
      noTone( buzzerPin );
    }

    if (isnan(input) || (input < lowerTripLimit) || (input > upperTripLimit) || tripped)
    {
      output = 0;
      tripped = true;
#ifndef OSPID_SILENT      
      tone( buzzerPin, 1000 ); // continuous beep - could get pretty annoying
#endif      
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
  if (!theInputDevice->getInitializationStatus())
  {
    input = NAN;
    displayInput = (ospDecimalValue<1>){-19999}; // Display Err
    theInputDevice->initialize();
  }     

  now = millis();
  if (settingsWritebackNeeded && (now > settingsWritebackTime)) 
  {
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

/* FIXME commented out temporarily to save space
 *
 *
      processSerialCommand();
 *
 *
 */

      serialCommandLength = 0;
    }
  }
}


