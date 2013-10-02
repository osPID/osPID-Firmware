#ifndef OSPCONFIG_H
#define OSPCONFIG_H

// global defines file included in the header of all .ino files

// the controller name displayed in the startup banner and the identifY response
#define CONTROLLER_NAME "Stripboard osPID"
PROGMEM const char PcontrollerName[] = CONTROLLER_NAME;

// the version tag displayed in the startup banner and the identifY response
#define VERSION_TAG "v1.0"
PROGMEM const char Pversion[] = VERSION_TAG;

// pin assignment for LCD display
enum
{
  lcdRsPin     = 2, 
  lcdEnablePin = 3, 
  lcdD0Pin     = 7, 
  lcdD1Pin     = 6, 
  lcdD2Pin     = 5, 
  lcdD3Pin     = 4
};

// pin assignment for analogue buttons
enum { buttonsPin = A4 };

// pin assignment for buzzer
enum { buzzerPin = A5 };

// pin assignments for input devices 
enum { thermistorPin  = A0 };
enum { oneWireBus     = A0 };
enum 
{ 
  thermocoupleSO_Pin  = A0,
  thermocoupleCS_Pin  = A1,
  thermocoupleCLK_Pin = A2 
}; 

// quiet mode (buzzer off) 
#undef SILENCE_BUZZER

// use Fahrenheit temperature units
// NB This option only changes the units for the
//    input sensor measurements. Temperature settings 
//    saved on the EEPROM -- set points, calibration values,
//    trip limits, and profile settings -- will not be upated
//    if this setting is changed.
#define UNITS_FAHRENHEIT

// use simulator for input/output
#undef USE_SIMULATOR

// omit serial processing commands to compile shorter
#undef SHORTER

// necessary omissions to compil on Atmega microcontrollers with 32 kB flash
#define ATMEGA_32kB_FLASH

// for longest options, #undef USE_SIMULATOR and SHORTER, #define UNITS_FAHRENHEIT

#endif
