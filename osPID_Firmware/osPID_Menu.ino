/* This file contains the implementation of the on-screen menu system of the controller */

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "MyLiquidCrystal.h"
#include "ospAssert.h"
#include "ospDecimalValue.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MENU"), __LINE__);


enum { firstDigitPosition = 4, lastDigitPosition = 8 };

enum { MENU_FLAG_2x2_FORMAT = 0x01 };

/*
 * This class encapsulates the PROGMEM tables which define the menu system.
 */
struct MenuItem 
{
  byte pmemItemCount;
  byte pmemFlags;
  const byte *pmemItemPtr;

  byte itemCount() const 
  {
    return pgm_read_byte_near(&pmemItemCount);
  }

  byte itemAt(byte index) const 
  {
    const byte *itemPtr = (const byte *)pgm_read_word_near(&pmemItemPtr);
    return pgm_read_byte_near(&itemPtr[index]);
  }

  bool is2x2() const 
  {
    return (pgm_read_byte_near(&pmemFlags) & MENU_FLAG_2x2_FORMAT);
  }
};


// all of the items which might be displayed on the screen
enum 
{
  // all menus must be first
  ITEM_MAIN_MENU = 0,
  ITEM_DASHBOARD_MENU,
  ITEM_PROFILE_MENU,
  ITEM_CONFIG_MENU,
  ITEM_SETPOINT_MENU,
  ITEM_TRIP_MENU,
  ITEM_INPUT_MENU,
  ITEM_POWERON_MENU,
  ITEM_COMM_MENU,
  ITEM_RESET_ROM_MENU,

  // then decimal items
  // NOTE: these must be the first N items in the decimalItemData[] array!
  FIRST_DECIMAL_ITEM,
  ITEM_SETPOINT = FIRST_DECIMAL_ITEM,
  ITEM_INPUT,
  ITEM_OUTPUT,
  ITEM_KP,
  ITEM_KI,
  ITEM_KD,
  ITEM_CALIBRATION,
  ITEM_WINDOW_LENGTH,
  ITEM_LOWER_TRIP_LIMIT,
  ITEM_UPPER_TRIP_LIMIT,

  // then generic/specialized items
  FIRST_ACTION_ITEM,
  ITEM_AUTOTUNE_CMD = FIRST_ACTION_ITEM,
  ITEM_PROFILE1,
  ITEM_PROFILE2,
  ITEM_PROFILE3,

  ITEM_SETPOINT1,
  ITEM_SETPOINT2,
  ITEM_SETPOINT3,
  ITEM_SETPOINT4,

  ITEM_PID_MODE,
  ITEM_PID_DIRECTION,
  ITEM_UNITS,

  ITEM_INPUT_THERMISTOR,
  ITEM_INPUT_ONEWIRE,
  ITEM_INPUT_THERMOCOUPLE,
  ITEM_INPUT_SIMULATOR,

  ITEM_COMM_9p6k,
  ITEM_COMM_14p4k,
  ITEM_COMM_19p2k,
  ITEM_COMM_28p8k,
  ITEM_COMM_38p4k,
  ITEM_COMM_57p6k,
  ITEM_COMM_115k,

  ITEM_POWERON_DISABLE,
  ITEM_POWERON_CONTINUE,
  ITEM_POWERON_RESUME_PROFILE,

  ITEM_TRIP_ENABLED,
  ITEM_TRIP_AUTORESET,

  ITEM_RESET_ROM_NO,
  ITEM_RESET_ROM_YES,

  ITEM_COUNT,
  MENU_COUNT = FIRST_DECIMAL_ITEM,
  DECIMAL_ITEM_COUNT = FIRST_ACTION_ITEM - FIRST_DECIMAL_ITEM,
  TEMPERATURE_ITEMS_LIST = 10
};

PROGMEM const byte mainMenuItems[4] = { ITEM_DASHBOARD_MENU, ITEM_PROFILE_MENU, ITEM_CONFIG_MENU, ITEM_AUTOTUNE_CMD };
PROGMEM const byte dashMenuItems[4] = { ITEM_SETPOINT, ITEM_INPUT, ITEM_OUTPUT, ITEM_PID_MODE };
PROGMEM const byte configMenuItems[12] = { ITEM_KP, ITEM_KI, ITEM_KD, ITEM_CALIBRATION, ITEM_WINDOW_LENGTH, ITEM_PID_DIRECTION, 
  ITEM_TRIP_MENU, ITEM_INPUT_MENU, ITEM_UNITS, ITEM_POWERON_MENU, ITEM_COMM_MENU, ITEM_RESET_ROM_MENU };
PROGMEM const byte profileMenuItems[3] = { ITEM_PROFILE1, ITEM_PROFILE2, ITEM_PROFILE3 };
PROGMEM const byte setpointMenuItems[4] = { ITEM_SETPOINT1, ITEM_SETPOINT2, ITEM_SETPOINT3, ITEM_SETPOINT4 };
#ifndef USE_SIMULATOR
PROGMEM const byte inputMenuItems[3] = { ITEM_INPUT_THERMISTOR, ITEM_INPUT_ONEWIRE, ITEM_INPUT_THERMOCOUPLE };
#else
PROGMEM const byte inputMenuItems[1] = { ITEM_SIMULATOR };
#endif
PROGMEM const byte commMenuItems[7] = { ITEM_COMM_9p6k, ITEM_COMM_14p4k, ITEM_COMM_19p2k, ITEM_COMM_28p8k,
  ITEM_COMM_38p4k, ITEM_COMM_57p6k, ITEM_COMM_115k };
PROGMEM const byte poweronMenuItems[3] = { ITEM_POWERON_DISABLE, ITEM_POWERON_CONTINUE, ITEM_POWERON_RESUME_PROFILE };
PROGMEM const byte tripMenuItems[4] = { ITEM_TRIP_ENABLED, ITEM_LOWER_TRIP_LIMIT, ITEM_UPPER_TRIP_LIMIT, ITEM_TRIP_AUTORESET };
PROGMEM const byte resetRomMenuItems[2] = { ITEM_RESET_ROM_NO, ITEM_RESET_ROM_YES };
PROGMEM const byte temperatureItems[4] = { ITEM_SETPOINT, ITEM_INPUT, ITEM_LOWER_TRIP_LIMIT, ITEM_UPPER_TRIP_LIMIT };

// This must be in the same order as the ITEM_*_MENU enumeration values
PROGMEM const MenuItem menuData[MENU_COUNT + 1] =
{ 
  { sizeof(mainMenuItems), 0, mainMenuItems               } ,
  { sizeof(dashMenuItems), 0, dashMenuItems               } ,
  { sizeof(profileMenuItems), 0, profileMenuItems         } ,
  { sizeof(configMenuItems), 0, configMenuItems           } ,
  { sizeof(setpointMenuItems), MENU_FLAG_2x2_FORMAT, setpointMenuItems               } ,
  { sizeof(tripMenuItems), 0, tripMenuItems               } ,
  { sizeof(inputMenuItems), 0, inputMenuItems             } ,
  { sizeof(poweronMenuItems), 0, poweronMenuItems         } ,
  { sizeof(commMenuItems), 0, commMenuItems               } ,
  { sizeof(resetRomMenuItems), 0, resetRomMenuItems       } ,
  // not a menu
  { sizeof(temperatureItems), 0, temperatureItems         }
};

/*
 * This class encapsulates the PROGMEM tables which describe how the various decimal
 * values are to be formatted.
 */
struct DecimalItem 
{
  char pmemIcon;
  byte pmemFlags;
  void *pmemValPtr;

  enum 
  {
    ONE_DECIMAL_PLACE = 0,
    TWO_DECIMAL_PLACES = 0x01,
    THREE_DECIMAL_PLACES = 0x02,
    RANGE_M9999_P9999 = 0,
    RANGE_0_1000 = 0x04,
    RANGE_0_32767 = 0x08,
    RANGE_1_32767 = 0x10,
    RANGE_M999_P999 = 0x20,
    NO_EDIT = 0x40,
    EDIT_MANUAL_ONLY = 0x80
  };

  byte flags() const 
  {
    return pgm_read_byte_near(&pmemFlags);
  }

  byte decimalPlaces() const
  {
    byte f = flags();
    if (f & TWO_DECIMAL_PLACES)
      return 2;
    if (f & THREE_DECIMAL_PLACES)
      return 3;
    return 1;
  }

  int minimumValue() const 
  {
    byte f = flags();
    if (f & RANGE_1_32767)
      return 1;
    if (f & (RANGE_0_1000 | RANGE_0_32767))
      return 0;
    if (f & RANGE_M999_P999)
      return -999;
    return -9999;
  }

  int maximumValue() const 
  {
    byte f = flags();
    if (f & RANGE_M999_P999)
      return 999;
    if (f & RANGE_0_1000)
      return 1000;
    if (f & (RANGE_0_32767 | RANGE_1_32767))
      return 32767;
    return 9999;
  }

  int currentValue() const 
  {
    int *p = (int *)pgm_read_word_near(&pmemValPtr);
    return *p;
  }
  
  void boundValue() const
  {
    int *p = (int *)pgm_read_word_near(&pmemValPtr);
    if (*p > this->maximumValue())
      *p = this->maximumValue();
    if (*p < this->minimumValue())
      *p = this->minimumValue();
  }

  int *valuePtr() const 
  {
    return (int *)pgm_read_word_near(&pmemValPtr);
  }

  char icon() const 
  {
    return pgm_read_byte_near(&pmemIcon);
  }
};

// This must be in the same order as the ITEM_* enumeration
PROGMEM DecimalItem decimalItemData[DECIMAL_ITEM_COUNT] =
{
  { 'S', DecimalItem::RANGE_M9999_P9999 | DecimalItem::ONE_DECIMAL_PLACE, &displaySetpoint },
  { 'I', DecimalItem::RANGE_M9999_P9999 | DecimalItem::ONE_DECIMAL_PLACE | DecimalItem::NO_EDIT, &displayInput },
  { 'O', DecimalItem::RANGE_0_1000 | DecimalItem::ONE_DECIMAL_PLACE | DecimalItem::EDIT_MANUAL_ONLY, &displayOutput },
  { 'P', DecimalItem::RANGE_0_32767 | DecimalItem::THREE_DECIMAL_PLACES, &PGain },
  { 'I', DecimalItem::RANGE_0_32767 | DecimalItem::THREE_DECIMAL_PLACES, &IGain },
  { 'D', DecimalItem::RANGE_0_32767 | DecimalItem::THREE_DECIMAL_PLACES, &DGain },
  { 'C', DecimalItem::RANGE_M999_P999 | DecimalItem::ONE_DECIMAL_PLACE, &displayCalibration },
  { 'W', DecimalItem::RANGE_1_32767 | DecimalItem::ONE_DECIMAL_PLACE, &displayWindow },
  { 'L', DecimalItem::RANGE_M9999_P9999 | DecimalItem::ONE_DECIMAL_PLACE, &lowerTripLimit },
  { 'U', DecimalItem::RANGE_M9999_P9999 | DecimalItem::ONE_DECIMAL_PLACE, &upperTripLimit }
};

struct MenuStateData 
{
  byte currentMenu;
  byte firstItemMenuIndex;
  byte highlightedItemMenuIndex;
  byte editDepth;
  unsigned long editStartMillis;
  bool editing;
};

struct MenuStateData menuState;

// draw the initial startup banner
static void drawStartupBanner()
{
  // display a startup message
  theLCD.setCursor(0, 0);
  theLCD.println(" osPID");
  theLCD.setCursor(0, 1);
  theLCD.println(" " OSPID_VERSION_TAG);
#ifndef OSPID_SILENT  
  tone( buzzerPin, 1000, 10 );
#endif  
}

// draw a banner reporting a bad EEPROM checksum
static void drawBadCsum(byte profile)
{
  delay(500);
  theLCD.setCursor(0, 0);
  if (profile == 0xFF)
    theLCD.println(PSTR("Config"));
  else
  {
    theLCD.println(PSTR("Profile"));
    theLCD.setCursor(8, 0);
    theLCD.print(profile + 1);
  }
  theLCD.setCursor(0, 1);
  theLCD.println(PSTR("Cleared"));
  delay(2000);
}

// draw a banner reporting that we're resuming an interrupted profile
static void drawResumeProfileBanner()
{
  theLCD.setCursor(0, 0);
  theLCD.println(PSTR("Resuming"));
  theLCD.setCursor(0, 1);
  drawProfileName(activeProfileIndex);
  delay(1000);
}

static void drawMenu()
{
  byte itemCount = menuData[menuState.currentMenu].itemCount();

  if (menuData[menuState.currentMenu].is2x2())
  {
    // NOTE: right now the code only supports one screen (<= 4 items) in
    // 2x2 menu mode
    ospAssert(itemCount <= 4);

    for (byte i = 0; i < itemCount; i++) 
    {
      bool highlight = (i == menuState.highlightedItemMenuIndex);
      byte item = menuData[menuState.currentMenu].itemAt(i);

      drawHalfRowItem(i / 2, 4 * (i & 1), highlight, item);
    }
  }
  else
  {
    // 2x1 format; supports an arbitrary number of items in the menu
    bool highlightFirst = (menuState.highlightedItemMenuIndex == menuState.firstItemMenuIndex);
    ospAssert(menuState.firstItemMenuIndex + 1 < itemCount);

    drawFullRowItem(0, highlightFirst, menuData[menuState.currentMenu].itemAt(menuState.firstItemMenuIndex));
    drawFullRowItem(1, !highlightFirst, menuData[menuState.currentMenu].itemAt(menuState.firstItemMenuIndex+1));
  }

  // certain ongoing states flash a notification in the cursor slot
  drawStatusFlash();
}

// This function converts a decimal fixed-point integer to a string,
// using the requested number of decimals. The string value is
// right-justified in the buffer, and the return value is a pointer
// to the first character in the formatted value. Characters between
// the start of the buffer and the return value are left unchanged.
static char *formatDecimalValue(char buffer[7], int num, byte decimals)
{
  byte decimalPos = (decimals == 0) ? 255 : 5 - decimals;
  bool isNegative = (num < 0);
  num = abs(num);

  buffer[6] = '\0';
  for (byte i = 5; i >= 0; i--)
  {
    if (i == decimalPos)
    {
      buffer[i] = '.';
      continue;
    }
    if (num == 0)
    {
      if (i >= decimalPos - 1)
        buffer[i] = '0';
      else if (isNegative)
      {
        buffer[i] = '-';
        return &buffer[i];
      }
      else
        return &buffer[i+1];
    }
    byte digit = num % 10;
    num /= 10;
    buffer[i] = digit + '0';
  }
  return buffer;
}

// draw a floating-point item's value at the current position
static void drawDecimalValue(byte item)
{
  char buffer[lastDigitPosition + 1];
  memset(&buffer, ' ', lastDigitPosition + 1);
  byte itemIndex = item - FIRST_DECIMAL_ITEM;
  char icon = decimalItemData[itemIndex].icon();
  int num = decimalItemData[itemIndex].currentValue();
  const byte decimals = decimalItemData[itemIndex].decimalPlaces();

  // flash "Trip" for the setpoint if the controller has hit a limit trip
  if (tripped && (item == ITEM_SETPOINT))
  {
    theLCD.print(icon);
    if (now & 0x400)
      theLCD.print(F("   Trip"));
    else
      theLCD.spc(7);
    return;
  }
  if ((num == -19999) && (item == ITEM_INPUT))
  {
    // display an error
    theLCD.print(icon);
    if (now & 0x400)
      theLCD.print(F("    Err"));
    else
      theLCD.spc(7);
    return;
  }

  buffer[0] = icon;
  formatDecimalValue(&buffer[2], num, decimals);
  theLCD.print(buffer);
}

// can a given item be edited
static bool canEditDecimalItem(const byte index)
{
  byte flags = decimalItemData[index].flags();

  return !(flags & DecimalItem::NO_EDIT) &&
    !((flags & DecimalItem::EDIT_MANUAL_ONLY) && (modeIndex == 1));
}

static bool canEditItem(byte item)
{
  bool canEdit = !tuning;

  if (item < FIRST_DECIMAL_ITEM)
    canEdit = true; // menus always get a '>' selector
  else if (item < FIRST_ACTION_ITEM)
    canEdit = canEdit && canEditDecimalItem(item - FIRST_DECIMAL_ITEM);

  return canEdit;
}

// draw the selector character at the current position
static void drawSelector(byte item, bool selected)
{
  if (!selected)
  {
    theLCD.print(' ');
    return;
  }

  bool canEdit = canEditItem(item);

  if (menuState.editing && !canEdit && (millis() > menuState.editStartMillis + 1000))
  {
    // cancel the disallowed edit
    stopEditing(item);
  }

  if (menuState.editing)
    theLCD.print(canEdit ? '[' : '!');
  else
    theLCD.print(canEdit ? '>' : '|');
}

// draw a profile name at the current position
static void drawProfileName(byte profileIndex)
{
  for (byte i = 0; i < 15; i++)
  {
    char ch = getProfileNameCharAt(profileIndex, i);
    theLCD.print(ch ? ch : ' ');
  }
}

// draw an item occupying a full 8x1 display line
static void drawFullRowItem(byte row, bool selected, byte item)
{
  long int kbps;
  theLCD.setCursor(0, row);

  // first draw the selector
  drawSelector(item, selected);

  // then draw the item
  if ((item >= FIRST_DECIMAL_ITEM) && (item < FIRST_ACTION_ITEM))
  {
    drawDecimalValue(item);
    switch (item)
    { 
    case ITEM_SETPOINT:
    case ITEM_INPUT:
    case ITEM_CALIBRATION:
    case ITEM_LOWER_TRIP_LIMIT:
    case ITEM_UPPER_TRIP_LIMIT:
      theLCD.print(F(" \337"));
      if (displayCelsius)
        theLCD.print('C');
      else
        theLCD.print('F');
      break;
    case ITEM_WINDOW_LENGTH:
      theLCD.print(F(" s "));
    default:
      theLCD.spc(3);
    }
    theLCD.spc(4);
  }
  else switch (item)
  {
  case ITEM_DASHBOARD_MENU:
    theLCD.println(PSTR("Dashboard"));
    break;
  case ITEM_CONFIG_MENU:
    theLCD.println(PSTR("Configure"));
    break;
  case ITEM_PROFILE_MENU:
    if (runningProfile)
      theLCD.println(PSTR("Cancel"));
    else
      drawProfileName(activeProfileIndex);
    break;
    // case ITEM_SETPOINT_MENU: should not happen
  case ITEM_COMM_MENU:
    theLCD.println(PSTR("Communication"));
    break;
  case ITEM_POWERON_MENU:
    theLCD.println(PSTR("Power On"));
    break;
  case ITEM_TRIP_MENU:
    theLCD.println(PSTR("Alarm"));
    break;
  case ITEM_INPUT_MENU:
    theLCD.println(PSTR("Input"));
    break;
  case ITEM_RESET_ROM_MENU:
    theLCD.println(PSTR("Reset Memory"));
    break;
  case ITEM_AUTOTUNE_CMD:
    if (tuning)
      theLCD.println(PSTR("Cancel"));
    else
      theLCD.println(PSTR("Auto Tuning"));
    break;
  case ITEM_PROFILE1:
  case ITEM_PROFILE2:
  case ITEM_PROFILE3:
    drawProfileName(item - ITEM_PROFILE1);
    break;
    //case ITEM_SETPOINT1:
    //case ITEM_SETPOINT2:
    //case ITEM_SETPOINT3:
    //case ITEM_SETPOINT4: should not happen
  case ITEM_PID_MODE:
    if (modeIndex == MANUAL)
      theLCD.println(PSTR("Manual Control"));
    else
      theLCD.println(PSTR("PID Control"));
    break;
  case ITEM_PID_DIRECTION:
    if (ctrlDirection == DIRECT)
      theLCD.println(PSTR("Action Forward"));
    else
      theLCD.println(PSTR("Action Reverse"));
    break;
  case ITEM_UNITS:
    if (displayCelsius)
      theLCD.println(PSTR("Celsius"));
    else
      theLCD.println(PSTR("Fahrenheit"));
    break;
  case ITEM_INPUT_THERMISTOR:
    theLCD.println(PSTR("Thermistor"));
    break;
  case ITEM_INPUT_ONEWIRE:   
    theLCD.println(PSTR("DS18B20+"));
    break;
  case ITEM_INPUT_THERMOCOUPLE:
    theLCD.println(PSTR("Thermocouple"));
    break;
  case ITEM_INPUT_SIMULATOR:
    theLCD.println(PSTR("Simulation"));
    break;
  case ITEM_COMM_9p6k:
  case ITEM_COMM_14p4k:
  case ITEM_COMM_19p2k:
  case ITEM_COMM_28p8k:
  case ITEM_COMM_38p4k:
  case ITEM_COMM_57p6k:
  case ITEM_COMM_115k:
    kbps = pgm_read_dword_near(&serialSpeedTable[item - ITEM_COMM_9p6k]);
    theLCD.print(kbps);
    theLCD.println(PSTR(" baud"));
    theLCD.spc(5 + (item == ITEM_COMM_9p6k) + (item < ITEM_COMM_115k));
    break;
  case ITEM_POWERON_DISABLE:
    theLCD.println(PSTR("Disable"));
    break;
  case ITEM_POWERON_CONTINUE:
    theLCD.println(PSTR("Continue"));
    break;
  case ITEM_POWERON_RESUME_PROFILE:
    theLCD.println(PSTR("Resume Profile"));
    break;
  case ITEM_TRIP_ENABLED:
    if (tripLimitsEnabled)
      theLCD.println(PSTR("Alarm Enabled")); 
    else
      theLCD.println(PSTR("Alarm Disabled"));
    break;
  case ITEM_TRIP_AUTORESET:
    if (tripAutoReset)
      theLCD.println(PSTR("Auto Reset"));
    else
      theLCD.println(PSTR("Manual Reset"));
    break;
  case ITEM_RESET_ROM_NO:
    theLCD.println(PSTR("No"));
    break;
  case ITEM_RESET_ROM_YES:
    theLCD.println(PSTR("Yes"));
    break;
  default:
    BUGCHECK();
  }
}


// flash a status indicator if appropriate
static void drawStatusFlash()
{
  byte flashState = (( millis() & 0xC00 ) >> 10);

  char ch = 0;
  if (tripped && (flashState > 0))
  {   
    if ((flashState & 1) > 0) 
    {
      ch = '!';
    }
  }
  else if (tuning && (flashState > 0))
  {
    if ((flashState & 1) > 0) 
    {
      ch = 't';
    }
  }
  else if (runningProfile && (flashState > 1))
  {
    if (flashState == 2)
      ch = 'P';
    else
    {
      ch = hex(currentProfileStep);
    }
  }
  else
    ch = 0;
  drawNotificationCursor(0);
}

// draw an item which takes up half a row (4 characters),
// for 2x2 menu mode
static void drawHalfRowItem(byte row, byte col, bool selected, byte item)
{
  theLCD.setCursor(col, row);

  drawSelector(item, selected);

  switch (item)
  {
  theLCD.print(F("SP"));  
  case ITEM_SETPOINT1:
  case ITEM_SETPOINT2:
  case ITEM_SETPOINT3:
  case ITEM_SETPOINT4:
    theLCD.print((char)'1' + item - ITEM_SETPOINT1);
    break;
  default:
    BUGCHECK();
  }
}

// draw a character at the current location of the selection indicator
// (it will be overwritten at the next screen redraw)
//
// if icon is '\0', then just set the cursor at the editable location
static void drawNotificationCursor(char icon)
{
  if (menuData[menuState.currentMenu].is2x2())
  {
    ospAssert(!menuState.editing);

    if (!icon)
      return;

    byte row = menuState.highlightedItemMenuIndex / 2;
    byte col = 4 * (menuState.highlightedItemMenuIndex & 1);

    theLCD.setCursor(col, row);
    theLCD.print(icon);
    return;
  }

  byte row = (menuState.highlightedItemMenuIndex == menuState.firstItemMenuIndex) ? 0 : 1;

  if (icon)
  {
    theLCD.setCursor(0, row);
    theLCD.print(icon);
  }

  if (menuState.editing)
    theLCD.setCursor(menuState.editDepth, row);
}

static void startEditing(byte item)
{
  menuState.editing = true;
  if (item < FIRST_ACTION_ITEM)
    menuState.editDepth = firstDigitPosition;
  else
    menuState.editDepth = 1;

  menuState.editStartMillis = millis();

  if (canEditItem(item))
    theLCD.cursor();
}

static void stopEditing(byte item)
{
  menuState.editing = false;
  theLCD.noCursor();
  if ((item == ITEM_UNITS) && changeUnitsFlag)
    switchUnits();
}

static void switchUnits()
{
  if (!changeUnitsFlag)
    return; // shouldn't happen
    
  // switch units for displaySetpoint, displayInput, and trip limits:
  for (byte i = 0; i < menuData[TEMPERATURE_ITEMS_LIST].itemCount(); i++)
  {
    byte decimalItemIndex = menuData[TEMPERATURE_ITEMS_LIST].itemAt(i) - FIRST_DECIMAL_ITEM;
    /*
    ospDecimalValue<1> t = (ospDecimalValue<1>){decimalItemData[decimalItemIndex].currentValue()};
    t = (displayCelsius ? convertFtoC(t) : convertCtoF(t)); // fails for some reason
    */
    long t = decimalItemData[decimalItemIndex].currentValue();
    t = (displayCelsius ? (((t - 320) * 10) / 18) : ((t * 18) / 10) + 320);
    int *valPtr = decimalItemData[decimalItemIndex].valuePtr();
    *valPtr = (int) t;
    decimalItemData[decimalItemIndex].boundValue();
  }
  
  // calibration parameter is different because 32F is not added 
  if (displayCelsius)
    displayCalibration = (displayCalibration / (ospDecimalValue<1>){18}).rescale<1>();
  else
    displayCalibration = (displayCalibration * (ospDecimalValue<1>){18}).rescale<1>();
  decimalItemData[ITEM_CALIBRATION - FIRST_DECIMAL_ITEM].boundValue();
  
  // change setPoint values 
  for (byte i = 0; i < 4; i++ )
  {
    setPoints[i] = (displayCelsius ? convertFtoC(setPoints[i]) : convertCtoF(setPoints[i]));
  }

  // profile information will stay in Celsius
  
  //reset flag
  changeUnitsFlag = false;
}

static void backKeyPress()
{
  if (menuState.editing)
  {
    // step the cursor one place to the left and stop editing if required
    menuState.editDepth--;
    byte item = menuData[menuState.currentMenu].itemAt(menuState.highlightedItemMenuIndex);
    if (item < FIRST_ACTION_ITEM)
    {
      // floating-point items have a decimal point, which we want to jump over
      if (menuState.editDepth == lastDigitPosition - decimalItemData[item - FIRST_DECIMAL_ITEM].decimalPlaces())
        menuState.editDepth--;
    }

    if (menuState.editDepth < firstDigitPosition)
      stopEditing(item);   

    return;
  }

  // go up a level in the menu hierarchy
  byte prevMenu = menuState.currentMenu;
  switch (prevMenu)
  {
  case ITEM_MAIN_MENU:
    break;
  case ITEM_DASHBOARD_MENU:
  case ITEM_CONFIG_MENU:
  case ITEM_PROFILE_MENU:
    menuState.currentMenu = ITEM_MAIN_MENU;
    menuState.highlightedItemMenuIndex = prevMenu - ITEM_DASHBOARD_MENU;
    menuState.firstItemMenuIndex = prevMenu - ITEM_DASHBOARD_MENU;
    break;
  case ITEM_SETPOINT_MENU:
    menuState.currentMenu = ITEM_DASHBOARD_MENU;
    menuState.highlightedItemMenuIndex = 0;
    menuState.firstItemMenuIndex = 0;
    break;
  case ITEM_TRIP_MENU:
  case ITEM_INPUT_MENU:
  case ITEM_POWERON_MENU:
  case ITEM_COMM_MENU:
  case ITEM_RESET_ROM_MENU:
    menuState.currentMenu = ITEM_CONFIG_MENU;
    menuState.highlightedItemMenuIndex = prevMenu - ITEM_TRIP_MENU + 6;
    menuState.firstItemMenuIndex = menuState.highlightedItemMenuIndex - 1;
    break;
  default:
    BUGCHECK();
  }
}

static void updownKeyPress(bool up)
{
  if (!menuState.editing)
  {
    // menu navigation
    if (up)
    {
      if (menuState.highlightedItemMenuIndex == 0)
        return;

      if (menuData[menuState.currentMenu].is2x2())
      {
        menuState.highlightedItemMenuIndex--;
        return;
      }

      if (menuState.highlightedItemMenuIndex == menuState.firstItemMenuIndex)
        menuState.firstItemMenuIndex--;

      menuState.highlightedItemMenuIndex = menuState.firstItemMenuIndex;
      return;
    }

    // down
    byte menuItemCount = menuData[menuState.currentMenu].itemCount();

    if (menuState.highlightedItemMenuIndex == menuItemCount - 1)
      return;

    menuState.highlightedItemMenuIndex++;

    if (menuData[menuState.currentMenu].is2x2())
      return;

    if (menuState.highlightedItemMenuIndex != menuState.firstItemMenuIndex + 1)
      menuState.firstItemMenuIndex = menuState.highlightedItemMenuIndex - 1;
    return;
  }

  // editing a number or a setting
  byte item = menuData[menuState.currentMenu].itemAt(menuState.highlightedItemMenuIndex);

  if (!canEditItem(item))
    return;

  // _something_ is going to change, so the settings are now dirty
  markSettingsDirty();

  if (item >= FIRST_ACTION_ITEM)
  {
    switch (item)
    {
    case ITEM_PID_MODE:
      modeIndex ^= 1; //= (modeIndex == 0 ? 1 : 0);
      // use the manual output value
      if (modeIndex == MANUAL)
        output = double(manualOutput);
      myPID.SetMode(modeIndex);
      break;
    case ITEM_PID_DIRECTION:
      ctrlDirection ^= 1; //= (ctrlDirection == 0 ? 1 : 0);
      myPID.SetControllerDirection(ctrlDirection);
      break;
    case ITEM_TRIP_ENABLED:
      tripLimitsEnabled = !tripLimitsEnabled;
      break;
    case ITEM_TRIP_AUTORESET:
      tripAutoReset = !tripAutoReset;
      break;
    case ITEM_UNITS:
      displayCelsius = !displayCelsius;  
      changeUnitsFlag = !changeUnitsFlag;
      break;
    default:
      BUGCHECK();
    }
    return;
  }

  // not a setting: must be a number

  // determine how much to increment or decrement
  const byte itemIndex = item - FIRST_DECIMAL_ITEM;
  byte decimalPointPosition = lastDigitPosition - decimalItemData[itemIndex].decimalPlaces();
  int increment = pow10(lastDigitPosition - menuState.editDepth - (menuState.editDepth < decimalPointPosition ? 1 : 0));

  if (!up)
    increment = -increment;

  // do the in/decrement and clamp it
  int val = decimalItemData[itemIndex].currentValue();
  int *valPtr = decimalItemData[itemIndex].valuePtr();
  *valPtr = val + increment;
  decimalItemData[itemIndex].boundValue();

  if (item == ITEM_SETPOINT)
    setPoints[setpointIndex] = displaySetpoint;
}

static int bound(int val, byte decimalItemIndex)
{
  int min = decimalItemData[decimalItemIndex].minimumValue();
  if (val < min)
    val = min;

  int max = decimalItemData[decimalItemIndex].maximumValue();
  if (val > max)
    val = max;
    
  return val;
}

static void okKeyPress()
{
  byte item = menuData[menuState.currentMenu].itemAt(menuState.highlightedItemMenuIndex);

  if (menuState.editing)
  {
    // step the cursor one digit to the right, or stop editing
    // if it has advanced off the screen
    menuState.editDepth++;
    if (item < FIRST_ACTION_ITEM)
    {
      // floating-point items have a decimal point, which we want to jump over
      if (menuState.editDepth == lastDigitPosition - decimalItemData[item - FIRST_DECIMAL_ITEM].decimalPlaces())
        menuState.editDepth++;
    }

    if ((menuState.editDepth > lastDigitPosition) || (item >= FIRST_ACTION_ITEM))
      stopEditing(item);

    return;
  }

  if (item < FIRST_DECIMAL_ITEM)
  {
    // the profile menu is special: a short-press on it triggers the
    // profile or cancels one that's in progress
    if (item == ITEM_PROFILE_MENU)
    {
      if (runningProfile)
        stopProfile();
      else if (!tuning)
        startProfile();
      return;
    }

    if (item == ITEM_DASHBOARD_MENU)
    {
      displayWindow = makeDecimal<1>(theOutputDevice->getOutputWindowSeconds());
    }

    // it's a menu: open that menu
    menuState.currentMenu = item;

    switch (item)
    {
    case ITEM_PROFILE_MENU:
      menuState.highlightedItemMenuIndex = activeProfileIndex;
      break;
    case ITEM_SETPOINT_MENU:
      menuState.highlightedItemMenuIndex = setpointIndex;
      break;
    case ITEM_INPUT_MENU:
      menuState.highlightedItemMenuIndex = inputType;
      break;
    case ITEM_COMM_MENU:
      menuState.highlightedItemMenuIndex = serialSpeed;
      break;
    case ITEM_POWERON_MENU:
      menuState.highlightedItemMenuIndex = powerOnBehavior;
      break;
    default:
      menuState.highlightedItemMenuIndex = 0;
      break;
    }
    menuState.firstItemMenuIndex = min(menuState.highlightedItemMenuIndex, menuData[menuState.currentMenu].itemCount() - 2);
    return;
  }

  if (item < FIRST_ACTION_ITEM)
  {
    // the setpoint flashes "Trip" if the unit has tripped; OK clears the trip
    if (tripped && (item == ITEM_SETPOINT))
    {
      tripped = false;
      output = double(manualOutput);
      noTone( buzzerPin );
      return;
    }
    // it's a numeric value: mark that the user wants to edit it
    // (the cursor will change if they can't)
    startEditing(item);
    return;
  }

  // it's an action item: some of them can be edited; others trigger
  // an action
  switch (item)
  {
  case ITEM_AUTOTUNE_CMD:
    if (runningProfile)
      break;
    if (!tuning)
      startAutoTune();
    else
      stopAutoTune();
    break;

  case ITEM_PROFILE1:
  case ITEM_PROFILE2:
  case ITEM_PROFILE3:
    activeProfileIndex = item - ITEM_PROFILE1;
    if (!tuning)
      startProfile();
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_SETPOINT1:
  case ITEM_SETPOINT2:
  case ITEM_SETPOINT3:
  case ITEM_SETPOINT4:
    setpointIndex = item - ITEM_SETPOINT1;
    displaySetpoint = setPoints[setpointIndex];
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_PID_MODE:
  case ITEM_PID_DIRECTION:
  case ITEM_TRIP_ENABLED:
  case ITEM_TRIP_AUTORESET:
  case ITEM_UNITS:
    startEditing(item);
    break;

  case ITEM_INPUT_THERMISTOR:
  case ITEM_INPUT_THERMOCOUPLE:
  case ITEM_INPUT_ONEWIRE:
  case ITEM_INPUT_SIMULATOR:
    // update inputType
    inputType = (item == ITEM_INPUT_SIMULATOR) ? 0 : (item - ITEM_INPUT_THERMISTOR);
    theInputDevice = inputDevice[inputType];
    theInputDevice->initialize();
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_COMM_9p6k:
  case ITEM_COMM_14p4k:
  case ITEM_COMM_19p2k:
  case ITEM_COMM_28p8k:
  case ITEM_COMM_38p4k:
  case ITEM_COMM_57p6k:
  case ITEM_COMM_115k:
    serialSpeed = (item - ITEM_COMM_9p6k);
    setupSerial();
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_POWERON_DISABLE:
  case ITEM_POWERON_CONTINUE:
  case ITEM_POWERON_RESUME_PROFILE:
    powerOnBehavior = (item - ITEM_POWERON_DISABLE);
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_RESET_ROM_NO:
    backKeyPress();
    break;
  case ITEM_RESET_ROM_YES:
    clearEEPROM();

    // perform a software reset by jumping to 0x0000, which is the start of the application code
    //
    // it would be better to use a Watchdog Reset
    typedef void (*VoidFn)(void);
    ((VoidFn) 0x0000)();
    break;
  default:
    BUGCHECK();
  }
}

// returns true if there was a long-press action; false if a long press
// is the same as a short press
static bool okKeyLongPress()
{
  byte item = menuData[menuState.currentMenu].itemAt(menuState.highlightedItemMenuIndex);

  // don't try to open menus while the user is editing a value
  if (menuState.editing)
    return false;

  // only two items respond to long presses: the setpoint and the profile menu
  if (item == ITEM_SETPOINT)
  {
    // open the setpoint menu
    menuState.currentMenu = ITEM_SETPOINT_MENU;
    menuState.firstItemMenuIndex = 0;
    menuState.highlightedItemMenuIndex = setpointIndex;
  }
  else if (item == ITEM_PROFILE_MENU)
  {
    // open the profile menu
    menuState.currentMenu = ITEM_PROFILE_MENU;
    menuState.highlightedItemMenuIndex = activeProfileIndex;
    menuState.firstItemMenuIndex = (activeProfileIndex == 0 ? 0 : 1);
  }
  else
    return false;

  return true;
}








