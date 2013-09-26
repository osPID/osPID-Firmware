/* This file contains implementations of various user-triggered actions */

#include "ospProfile.h"
#include "ospAssert.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("PROF"), __LINE__);

// Pin assignments on the main controller card (_not_ the I/O cards)
enum { buzzerPin = A5 };

// a program invariant has been violated: suspend the controller and
// just flash a debugging message until the unit is power cycled
void ospBugCheck(const char *block, int line)
{
    // note that block is expected to be PROGMEM

    theLCD.noCursor();

    theLCD.clear();
    for (int i = 0; i < 4; i++)
      theLCD.print((char)pgm_read_byte_near(&block[i]));
    theLCD.print(F(" Err"));

    theLCD.setCursor(0, 1);
    theLCD.print(F("Lin "));
    theLCD.print(line);

    // just lock up, flashing the error message
    while (true)
    {
      theLCD.display();
      delay(500);
      theLCD.noDisplay();
      delay(500);
    }
}

byte ATuneModeRemember;

static void startAutoTune()
{
  ATuneModeRemember = myPID.GetMode();
  myPID.SetMode(MANUAL);

  aTune.SetNoiseBand(double(aTuneNoise));
  aTune.SetOutputStep(double(aTuneStep));
  aTune.SetLookbackSec(aTuneLookBack);
  tuning = true;
}

static void stopAutoTune()
{
  aTune.Cancel();
  tuning = false;

  modeIndex = ATuneModeRemember;

  // restore the output to the last manual command; it will be overwritten by the PID
  // if the loop is active
  output = double(manualOutput);
  myPID.SetMode(modeIndex);
}

struct ProfileState 
{
  unsigned long stepEndMillis;
  unsigned long stepDuration;
  union 
  {
    ospDecimalValue<1> targetSetpoint;
    ospDecimalValue<1> maximumError;
  };
  ospDecimalValue<1> initialSetpoint;
  byte stepType;
  bool temperatureRising;
};

ProfileState profileState;

static void getProfileStepData(byte profileIndex, byte i, byte *type, unsigned long *duration, ospDecimalValue<1> *endpoint);

static bool startCurrentProfileStep()
{
  byte stepType;
  getProfileStepData(activeProfileIndex, currentProfileStep,
    &stepType, &profileState.stepDuration, &profileState.targetSetpoint);

  if (stepType == ospProfile::STEP_INVALID)
    return false;

  if (stepType & ospProfile::STEP_FLAG_BUZZER)
    digitalWrite(buzzerPin, HIGH);
  else
    digitalWrite(buzzerPin, LOW);

  profileState.stepType = stepType & ospProfile::STEP_TYPE_MASK;
  profileState.stepEndMillis = now + profileState.stepDuration;

  switch (profileState.stepType)
  {
  case ospProfile::STEP_RAMP_TO_SETPOINT:
    profileState.initialSetpoint = fakeSetpoint;
    break;
  case ospProfile::STEP_SOAK_AT_VALUE:
    // targetSetpoint is actually maximumError
    break;
  case ospProfile::STEP_JUMP_TO_SETPOINT:
    setpoint = double(profileState.targetSetpoint);
    break;
  case ospProfile::STEP_WAIT_TO_CROSS:
    profileState.temperatureRising = (input < double(profileState.targetSetpoint));
    break;
  default:
    return false;
  }

  return true;
}

// this function gets called every iteration of loop() while a profile is
// running
static void profileLoopIteration()
{
  ospDecimalValue<1> delta, fakeInput = makeDecimal<1>(input);
  ospAssert(!tuning);
  ospAssert(runningProfile);

  switch (profileState.stepType)
  {
  case ospProfile::STEP_RAMP_TO_SETPOINT:
    if (now >= profileState.stepEndMillis)
    {
      setpoint = double(profileState.targetSetpoint);
      break;
    }
    delta = profileState.targetSetpoint - profileState.initialSetpoint;
    // FIXME: does this handle rounding correctly?
    fakeSetpoint = profileState.targetSetpoint - makeDecimal<1>(
        int(long(delta.rawValue()) * (profileState.stepEndMillis - now) / profileState.stepDuration));
    return;
  case ospProfile::STEP_SOAK_AT_VALUE:
    delta = abs(fakeSetpoint - fakeInput);
    if (delta > profileState.maximumError)
      profileState.stepEndMillis = now + profileState.stepDuration;
    // fall through
  case ospProfile::STEP_JUMP_TO_SETPOINT:
    if (now < profileState.stepEndMillis)
      return;
    break;
  case ospProfile::STEP_WAIT_TO_CROSS:
    if ((fakeInput < profileState.targetSetpoint) && profileState.temperatureRising)
      return; // not there yet
    if ((fakeInput > profileState.targetSetpoint) && !profileState.temperatureRising)
      return;
    break;
  }

  // this step is done: load the next one if it exists
  recordProfileStepCompletion(currentProfileStep);
  if (currentProfileStep < ospProfile::NR_STEPS) 
  {
    currentProfileStep++;
    if (startCurrentProfileStep()) // returns false if there are no more steps
      return;
  }

  // the profile is finished
  stopProfile();
}

static void startProfile()
{
  ospAssert(!runningProfile);

  currentProfileStep = 0;
  recordProfileStart();
  runningProfile = true;

  if (!startCurrentProfileStep())
    stopProfile();
}

static void stopProfile()
{
  ospAssert(runningProfile);

  digitalWrite(buzzerPin, LOW);
  recordProfileCompletion();
  runningProfile = false;
}

