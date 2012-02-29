//header files for locally copied libraries appened with "_local"
//so as not to conflict with any exising copies of the libraries
//referenced by the IDE

#include "AnalogButton_local.h"
#include "max6675_local.h"
//#include "MAX31855.h"
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <avr/pgmspace.h>

// ***** PIN ASSIGNMENTS *****
const byte thermistorPin = A6;
const byte RelayPin = 5;
const byte SSRPin = 6;
const byte thermocoupleCS = 10;
const byte thermocoupleSO = 12;
const byte thermocoupleCLK = 13;
const byte buzzerPin = 3;
const byte systemLEDPin = A2;
double THERMISTORNOMINAL = 10;
double BCOEFFICIENT = 1;
double TEMPERATURENOMINAL = 293.15;

// ***** DEGREE SYMBOL FOR LCD *****
unsigned char degree[8]  = {
  140,146,146,140,128,128,128,128};
// 8x2 LCD interface
LiquidCrystal lcd(A1, A0, 4, 7, 8, 9);
// Analog button on pin A
AnalogButton button(A3, 0, 253, 454, 657);
// Specify MAX31855 thermocouple interface
MAX6675 thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO);
//MAX31855 thermocouple(thermocoupleSO, thermocoupleCS, thermocoupleCLK);

byte ATuneModeRemember = 0;
double input = 80, output = 50, setpoint = 180;
double kp = 2, ki = 0.5, kd = 2;

double kpmodel = 1.5, taup = 100, theta[50];
double outputStart = 5;
double aTuneStep = 20, aTuneNoise = 1;
unsigned int aTuneLookBack = 10;

double outWindowSec = 5.0;
unsigned long WindowSize = 5000;

boolean tuning = false;
unsigned long buttonTime, windowStartTime, tempTime,blinkTime, modelTime, lcdTime, serialTime;
byte menuIndex = 0;
byte itemIndex = 0,lastIndex = 0;
byte cursorX = 0, cursorY = 0;
byte serialAcknowledge = 0;
PID myPID(&input, &output, &setpoint,kp,ki,kd, DIRECT);
PID_ATune aTune(&input, &output);

boolean useSimulation = false;

int eepromTuningOffset=1;
byte ctrlDirection=DIRECT;
union {
  byte asBytes[12];
  float asFloats[3];
} 
eepromTunings; 

int eepromIOTypeOffset = 20;
byte inputType = 0;
byte outputType = 0;

int eepromWindowSizeOffset = 30;
union {
  byte asBytes[4];
  unsigned long asLong;
} 
eepromWindowSize;

int eepromATuneOffset = 50;
union {
  byte asBytes[12];
  float asFloats[3];
} 
eepromATune;

int eepromThermistorOffset = 70;
union {
  byte asBytes[12];
  float asFloats[3];
}
eepromThermistor;

int eepromDashOffset = 120;
union {
  byte asBytes[8];
  float asFloats[2];
} 
eepromDash;

void setup()
{
  Serial.begin(115200);
  lcd.begin(8, 2);

  pinMode(RelayPin, OUTPUT);
  pinMode(SSRPin, OUTPUT);

  initializeEEPROM();

  if(useSimulation) for(byte i=0;i<50;i++)  {    
    theta[i]=outputStart;  
  }

  windowStartTime = millis();
  tempTime=millis();
  lcdTime=10;
  blinkTime = 0;
  modelTime = 0;
  serialTime = 0;
  // Start of remove
  pinMode(2, OUTPUT);
  digitalWrite(2, (inputType == 2 ? true : false));
  // End of remove
  myPID.SetSampleTime(1000);
  myPID.SetOutputLimits(0, 100);
  myPID.SetTunings(kp, ki, kd);
  menuIndex = 0;
  buttonTime = millis();
  DrawLCD();
  // Ensure both relay and SSR is turned off.
  digitalWrite(RelayPin,LOW);
  digitalWrite(SSRPin,LOW);
}

void loop()
{
  unsigned long now = millis();

  if(now >= buttonTime)
  {
    switch(button.get())
    {
    case BUTTON_NONE:
      break;

    case BUTTON_RETURN:
      back();
      break;

    case BUTTON_UP:
      up();
      break;

    case BUTTON_DOWN:
      down();
      break;

    case BUTTON_OK:
      ok();
      break;
    }
    buttonTime += 50;
  }

  if(tuning)
  {
    byte val = (aTune.Runtime());

    if(val != 0)
    {
      tuning = false;
    }

    if(!tuning)
    { 
      // We're done, set the tuning parameters
      kp = aTune.GetKp();
      ki = aTune.GetKi();
      kd = aTune.GetKd();
      myPID.SetTunings(kp, ki, kd);
      AutoTuneHelper(false);
      EEPROMBackupTunings();
    }
  }
  else myPID.Compute();
  if(useSimulation) theta[30] = output;

  if(now >= lcdTime)
  {
    DrawLCD();
    lcdTime += 200;
  }
  if(now >= modelTime)
  {
    if(useSimulation) DoModel();
    else
    {
      if(inputType == 0) input = thermocouple.readCelsius();
      else if(inputType == 1 || inputType == 2) input = readThermistorTemp(analogRead(thermistorPin));
    }
    modelTime += 100; 

    unsigned long wind = (now - windowStartTime);
    if(wind>WindowSize)
    { 
      wind -= WindowSize;
      windowStartTime += WindowSize;
    }
    unsigned long oVal = (unsigned long)(output*(double)WindowSize/ 100.0);
    if(outputType == 0) digitalWrite(RelayPin ,(oVal>wind) ? HIGH : LOW);
    else if(outputType == 1) digitalWrite(SSRPin ,(oVal>wind) ? HIGH : LOW);
  } 

  // Send-receive with processing if it's time
  if(millis() > serialTime)
  {
    SerialReceive();
    SerialSend();
    serialTime += 500;
  }
}

void DoModel()
{
  // Cycle the dead time
  for(byte i=0;i<49;i++)
  {
    theta[i] = theta[i+1];
  }
  // Compute the input
  input = (kpmodel / taup) *(theta[0]-outputStart) + input*(1-1/taup) + ((float)random(-10,10))/100;
}




