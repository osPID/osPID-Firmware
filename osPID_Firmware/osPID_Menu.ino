/* This file contains the implementation of the on-screen menu system of the controller */

#include <avr/pgmspace.h>
#include "ospAssert.h"

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MENU"), __LINE__);

enum { TYPE_NAV = 0, TYPE_VAL = 1, TYPE_OPT = 2 };

enum {
  FLOAT_FLAG_1_DECIMAL_PLACE = 0x10,
  FLOAT_FLAG_2_DECIMAL_PLACES = 0,
  FLOAT_FLAG_RANGE_0_100 = 0x04,
  FLOAT_FLAG_RANGE_0_99 = 0x20,
  FLOAT_FLAG_RANGE_M999_P999 = 0,
  FLOAT_FLAG_NO_EDIT = 0x40,
  FLOAT_FLAG_EDIT_MANUAL_ONLY = 0x80
};

struct FloatItem {
  char pmemIcon;
  byte pmemFlags;
  double *pmemFPtr;

  byte decimalPlaces() const {
    return (pgm_read_byte_near(&pmemFlags) & FLOAT_FLAG_1_DECIMAL_PLACE ? 1 : 2);
  }

  double minimumValue() const {
    return (pgm_read_byte_near(&pmemFlags) & FLOAT_FLAG_RANGE_0_99 ? 0 : -999.9);
  }

  double maximumValue() const {
    byte flags = pgm_read_byte_near(&pmemFlags);
    if (flags & FLOAT_FLAG_RANGE_0_100)
      return 100.0;
    if (flags & FLOAT_FLAG_RANGE_0_99)
      return 99.99;
    return 999.9;
  }

  double currentValue() const {
    double *fp = (double *)pgm_read_word_near(&pmemFPtr);
    return *fp;
  }

  double *valuePtr() const {
    return (double *)pgm_read_word_near(&pmemFPtr);
  }

  char icon() const {
    return pgm_read_byte_near(&pmemIcon);
  }

  bool canEdit() const {
    byte flags = pgm_read_byte_near(&pmemFlags);

    return !(flags & FLOAT_FLAG_NO_EDIT) && 
        !((flags & FLOAT_FLAG_EDIT_MANUAL_ONLY) && (modeIndex == 1));
  }
};

enum { MENU_FLAG_2x2_FORMAT = 0x01 };

struct MenuItem {
  byte pmemItemCount;
  byte pmemFlags;
  const byte *pmemItemPtr;

  byte itemCount() const {
    return pgm_read_byte_near(&pmemItemCount);
  }

  byte itemAt(byte index) const {
    return pgm_read_byte_near(&pmemItemPtr[index]);
  }

  bool is2x2() const {
    return (pgm_read_byte_near(&pmemFlags) & MENU_FLAG_2x2_FORMAT);
  }
};

// all of the items which might be displayed on the screen
enum {
  // all menus must be first
  ITEM_MAIN_MENU = 0,
  ITEM_DASHBOARD_MENU,
  ITEM_CONFIG_MENU,
  ITEM_PROFILE_MENU,
  ITEM_SETPOINT_MENU,
  ITEM_COMM_MENU,
  ITEM_POWERON_MENU,
  ITEM_TRIP_MENU,

  // then double items
  FIRST_FLOAT_ITEM,
  ITEM_SETPOINT = FIRST_FLOAT_ITEM,
  ITEM_INPUT,
  ITEM_OUTPUT,
  ITEM_KP,
  ITEM_KI,
  ITEM_KD,
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

  ITEM_COUNT,
  MENU_COUNT = FIRST_FLOAT_ITEM,
  FLOAT_ITEM_COUNT = FIRST_ACTION_ITEM - FIRST_FLOAT_ITEM
};

PROGMEM const byte mainMenuItems[4] = { ITEM_DASHBOARD_MENU, ITEM_PROFILE_MENU, ITEM_CONFIG_MENU, ITEM_AUTOTUNE_CMD };
PROGMEM const byte dashMenuItems[4] = { ITEM_SETPOINT, ITEM_INPUT, ITEM_OUTPUT, ITEM_PID_MODE };
PROGMEM const byte configMenuItems[7] = { ITEM_KP, ITEM_KI, ITEM_KD, ITEM_PID_DIRECTION, ITEM_TRIP_MENU, ITEM_POWERON_MENU, ITEM_COMM_MENU };
PROGMEM const byte profileMenuItems[3] = { ITEM_PROFILE1, ITEM_PROFILE2, ITEM_PROFILE3 };
PROGMEM const byte setpointMenuItems[4] = { ITEM_SETPOINT1, ITEM_SETPOINT2, ITEM_SETPOINT3, ITEM_SETPOINT4 };
PROGMEM const byte commMenuItems[7] = { ITEM_COMM_9p6k, ITEM_COMM_14p4k, ITEM_COMM_19p2k, ITEM_COMM_28p8k,
                                        ITEM_COMM_38p4k, ITEM_COMM_57p6k, ITEM_COMM_115k };
PROGMEM const byte poweronMenuItems[3] = { ITEM_POWERON_DISABLE, ITEM_POWERON_CONTINUE, ITEM_POWERON_RESUME_PROFILE };
PROGMEM const byte tripMenuItems[4] = { ITEM_TRIP_ENABLED, ITEM_LOWER_TRIP_LIMIT, ITEM_UPPER_TRIP_LIMIT, ITEM_TRIP_AUTORESET };

PROGMEM const MenuItem menuData[MENU_COUNT] =
{
  { sizeof(mainMenuItems), 0, mainMenuItems },
  { sizeof(dashMenuItems), 0, dashMenuItems },
  { sizeof(configMenuItems), 0, configMenuItems },
  { sizeof(profileMenuItems), 0, profileMenuItems },
  { sizeof(setpointMenuItems), MENU_FLAG_2x2_FORMAT, setpointMenuItems },
  { sizeof(commMenuItems), 0, commMenuItems },
  { sizeof(poweronMenuItems), 0, poweronMenuItems },
  { sizeof(tripMenuItems), 0, tripMenuItems }
};

PROGMEM const FloatItem floatItemData[FLOAT_ITEM_COUNT] =
{
  { 'S', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE, &setpoint },
  { 'I', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE | FLOAT_FLAG_NO_EDIT, &input },
  { 'O', FLOAT_FLAG_RANGE_0_100 | FLOAT_FLAG_1_DECIMAL_PLACE | FLOAT_FLAG_EDIT_MANUAL_ONLY, &output },
  { 'P', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &kp },
  { 'I', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &ki },
  { 'D', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &kd },
  { 'L', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE, &lowerTripLimit },
  { 'U', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE, &upperTripLimit }
};

struct MenuStateData {
  byte currentMenu;
  byte firstItemMenuIndex;
  byte highlightedItemMenuIndex;
  byte editDepth;
  unsigned long editStartMillis;
  bool editing;
};

struct MenuStateData menuState;

static void drawMenu()
{
  byte itemCount = menuData[menuState.currentMenu].itemCount();

  if (menuData[menuState.currentMenu].is2x2())
  {
    // NOTE: right now the code only supports one screen (<= 4 items) in
    // 2x2 menu mode
    ospAssert(itemCount <= 4);

    for (byte i = 0; i < itemCount; i++) {
      bool highlight = (i == menuState.highlightedItemMenuIndex);
      byte item = menuData[menuState.currentMenu].itemAt(i);

      drawHalfRowItem(i / 2, i % 2, highlight, item);
    }
    
    drawStatusFlash(menuState.highlightedItemMenuIndex / 2);
  }
  else
  {
    // 2x1 format; supports an arbitrary number of items in the menu
    bool highlightFirst = (menuState.highlightedItemMenuIndex == menuState.firstItemMenuIndex);

    drawFullRowItem(0, highlightFirst, menuData[menuState.currentMenu].itemAt(menuState.firstItemMenuIndex));
    drawFullRowItem(1, !highlightFirst, menuData[menuState.currentMenu].itemAt(menuState.firstItemMenuIndex+1));

    drawStatusFlash(highlightFirst ? 0 : 1);
  }
}

// draw a floating-point item's value at the current position
static void drawFloat(byte item)
{
  char buffer[8];
  byte itemIndex = item - FIRST_FLOAT_ITEM;
  char icon = floatItemData[itemIndex].icon();
  double val = floatItemData[itemIndex].currentValue();
  byte decimals = floatItemData[itemIndex].decimalPlaces();

  // flash "Trip" for the setpoint if the controller has hit a limit trip
  if (tripped && item == ITEM_SETPOINT)
  {
    theLCD.print(icon);
    theLCD.print(now % 2048 < 1024 ? F(" Trip ") : F("       "));
    return;
  }
  if (isnan(val))
  {
    // display an error
    theLCD.print(icon);
    theLCD.print(now % 2048 < 1024 ? F(" IOErr"):F("      "));
    return;
  }

  for (int i = 0; i < decimals; i++)
    val *= 10;

  int num = (int)round(val);

  buffer[0] = icon;

  bool isNeg = num < 0;
  if (isNeg)
    num = -num;

  bool didneg = false;
  byte decimalPointPosition = 6-decimals;

  for(byte i = 6; i >= 1; i--)
  {
    if (i == decimalPointPosition)
      buffer[i] = '.';
    else
    {
      if (num == 0)
      {
        if (i >= decimalPointPosition - 1) // one zero before the decimal point
          buffer[i]='0';
        else if (isNeg && !didneg) // minus sign comes before zeros
        {
          buffer[i] = '-';
          didneg = true;
        }
        else
          buffer[i] = ' '; // skip leading zeros
      }
      else
      {
        buffer[i] = num % 10 + '0';
        num /= 10;
      }
    }
  }

  buffer[7] = '\0';
  theLCD.print(buffer);
}

// can a given item be edited
static bool canEditItem(byte item)
{
  bool canEdit = !tuning;

  if (item < FIRST_FLOAT_ITEM)
    canEdit = true; // menus always get a '>' selector
  else if (item < FIRST_ACTION_ITEM)
    canEdit = canEdit && floatItemData[item - FIRST_FLOAT_ITEM].canEdit();
  else if (item == ITEM_PID_DIRECTION)
    canEdit = (modeIndex == AUTOMATIC);

  return canEdit;
}

// draw the selector character at the current position
static void drawSelector(byte item, bool selected)
{
  bool canEdit = canEditItem(item);

  if (menuState.editing && !canEdit && millis() > menuState.editStartMillis + 1000)
  {
    // cancel the disallowed edit
    menuState.editing = false;
  }

  if (!selected)
    theLCD.print(' ');
  else if (menuState.editing)
    theLCD.print(canEdit ? '[' : '|');
  else
    theLCD.print(canEdit ? '>' : '}');
}

// draw a profile name at the current position
static void drawProfileName(byte profileIndex)
{
  for (byte i = 0; i < 8; i++)
    theLCD.print(getProfileNameCharAt(profileIndex, i));
}

// draw an item occupying a full 8x1 display line
static void drawFullRowItem(byte row, bool selected, byte item)
{
  theLCD.setCursor(0,row);

  // first draw the selector
  drawSelector(item, selected);

  // then draw the item
  if (item >= FIRST_FLOAT_ITEM && item < FIRST_ACTION_ITEM)
    drawFloat(item);
  else switch (item)
  {
  case ITEM_DASHBOARD_MENU:
    theLCD.print(F("DashBrd"));
    break;
  case ITEM_CONFIG_MENU:
    theLCD.print(F("Config "));
    break;
  case ITEM_PROFILE_MENU:
    if (runningProfile)
      theLCD.print(F("Cancel "));
    else
      drawProfileName(activeProfileIndex);
    break;
  // case ITEM_SETPOINT_MENU: should not happen
  case ITEM_COMM_MENU:
    theLCD.print(F("Comm   "));
    break;
  case ITEM_POWERON_MENU:
    theLCD.print(F("PowerOn"));
    break;
  case ITEM_TRIP_MENU:
    theLCD.print(F("Trip   "));
    break;
  case ITEM_AUTOTUNE_CMD:
    theLCD.print(tuning ? F("Cancel ") : F("AutoTun"));
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
    theLCD.print(modeIndex==0 ? F("Mod Man") : F("Mod PID"));
    break;
  case ITEM_PID_DIRECTION:
    theLCD.print(ctrlDirection==0 ? F("Act Dir") : F("Act Rev"));
    break;
  case ITEM_COMM_9p6k:
    theLCD.print(F(" 9.6kbd"));
    break;
  case ITEM_COMM_14p4k:
    theLCD.print(F("14.4kbd"));
    break;
  case ITEM_COMM_19p2k:
    theLCD.print(F("19.2kbd"));
    break;
  case ITEM_COMM_28p8k:
    theLCD.print(F("28.8kbd"));
    break;
  case ITEM_COMM_38p4k:
    theLCD.print(F("38.4kbd"));
    break;
  case ITEM_COMM_57p6k:
    theLCD.print(F("57.6kbd"));
    break;
  case ITEM_COMM_115k:
    theLCD.print(F("115 kbd"));
    break;
  case ITEM_POWERON_DISABLE:
    theLCD.print(F("Disable"));
    break;
  case ITEM_POWERON_CONTINUE:
    theLCD.print(F("Hold   "));
    break;
  case ITEM_POWERON_RESUME_PROFILE:
    theLCD.print(F("Profile"));
    break;
  case ITEM_TRIP_ENABLED:
    theLCD.print(tripLimitsEnabled ? F("Enabled") : F("Disabld"));
    break;
  case ITEM_TRIP_AUTORESET:
    theLCD.print(tripAutoReset ? F("AReset ") : F("MReset "));
  default:
    BUGCHECK();
  }
}

// flash a status indicator if appropriate
static void drawStatusFlash(byte row)
{
  theLCD.setCursor(0,row);

  if (tripped)
  {
    if (now % 2048 < 700)
      theLCD.print('T');
    else if (now % 2048 < 1400)
      theLCD.print('R');
    else
      theLCD.print('P');
  }
  else if (tuning)
  {
    if (now % 2048 < 700)
      theLCD.print('t');
    else if (now % 2048 < 1400)
      theLCD.print('u');
    else
      theLCD.print('n');
  }
  else if (runningProfile)
  {
    if (now % 2048 < 700)
      theLCD.print('P');
    else if (now % 2048 < 1400)
    {
      char c;
      if (currentProfileStep < 10)
        c = currentProfileStep + '0';
      else c = currentProfileStep + 'A';
      theLCD.print(c);
    }
  }
}

// draw an item which takes up half a row (4 characters),
// for 2x2 menu mode
static void drawHalfRowItem(byte row, byte col, bool selected, byte item)
{
  theLCD.setCursor(col, row);

  drawSelector(item, selected);

  switch (item)
  {
  case ITEM_SETPOINT1:
    theLCD.print(F("SP1"));
    break;
  case ITEM_SETPOINT2:
    theLCD.print(F("SP2"));
    break;
  case ITEM_SETPOINT3:
    theLCD.print(F("SP3"));
    break;
  case ITEM_SETPOINT4:
    theLCD.print(F("SP4"));
    break;
  default:
    BUGCHECK();
  }
}

static void startEditing()
{
  menuState.editing = true;
  menuState.editDepth = 3;
  menuState.editStartMillis = millis();
  theLCD.cursor();
}

static void stopEditing()
{
  menuState.editing = false;
  theLCD.noCursor();
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
      if (menuState.editDepth == 7 - floatItemData[item - FIRST_FLOAT_ITEM].decimalPlaces())
        menuState.editDepth--;
    }

    if (menuState.editDepth < 3)
      stopEditing();

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
    menuState.highlightedItemMenuIndex = prevMenu - 1;
    menuState.firstItemMenuIndex = prevMenu - 1;
    break;
  case ITEM_SETPOINT_MENU:
    menuState.currentMenu = ITEM_DASHBOARD_MENU;
    menuState.highlightedItemMenuIndex = 0;
    menuState.firstItemMenuIndex = 0;
    break;
  case ITEM_COMM_MENU:
    menuState.currentMenu = ITEM_CONFIG_MENU;
    menuState.highlightedItemMenuIndex = 6;
    menuState.firstItemMenuIndex = 5;
    break;
  case ITEM_POWERON_MENU:
    menuState.currentMenu = ITEM_CONFIG_MENU;
    menuState.highlightedItemMenuIndex = 5;
    menuState.firstItemMenuIndex = 4;
    break;
  case ITEM_TRIP_MENU:
    menuState.currentMenu = ITEM_CONFIG_MENU;
    menuState.highlightedItemMenuIndex = 4;
    menuState.firstItemMenuIndex = 3;
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

      if (menuState.highlightedItemMenuIndex == menuState.firstItemMenuIndex)
        menuState.firstItemMenuIndex--;

      menuState.highlightedItemMenuIndex = menuState.firstItemMenuIndex;
      return;
    }

    // down
    byte menuItemCount = menuData[menuState.currentMenu].itemCount();

    if (menuState.highlightedItemMenuIndex == menuItemCount - 1)
      return;

    if (menuState.highlightedItemMenuIndex != menuState.firstItemMenuIndex)
      menuState.firstItemMenuIndex++;

    menuState.highlightedItemMenuIndex = menuState.firstItemMenuIndex;
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
      modeIndex = (modeIndex == 0 ? 1 : 0);
      myPID.SetMode(modeIndex);
      break;
    case ITEM_PID_DIRECTION:
      ctrlDirection = (ctrlDirection == 0 ? 1 : 0);
      myPID.SetControllerDirection(ctrlDirection);
      break;
    case ITEM_TRIP_ENABLED:
      tripLimitsEnabled = !tripLimitsEnabled;
      break;
    case ITEM_TRIP_AUTORESET:
      tripAutoReset = !tripAutoReset;
      break;
    default:
      BUGCHECK();
    }
    return;
  }

  // not a setting: must be a number

  // determine how much to increment or decrement
  byte decimalPointPosition = 7 - floatItemData[item - FIRST_FLOAT_ITEM].decimalPlaces();
  double increment = 1.0;

  byte pow10 = menuState.editDepth - decimalPointPosition;
  while (pow10-- > 0)
    increment *= 10.0;
  while (pow10++ < 0)
    increment *= 0.1;

  if (!up)
    increment = -increment;

  // do the in/decrement and clamp it
  double val = floatItemData[item - FIRST_FLOAT_ITEM].currentValue();
  val += increment;

  double min = floatItemData[item - FIRST_FLOAT_ITEM].minimumValue();
  if (val < min)
    val = min;

  double max = floatItemData[item - FIRST_FLOAT_ITEM].maximumValue();
  if (val > max)
    val = max;

  double *valPtr = floatItemData[item - FIRST_FLOAT_ITEM].valuePtr();
  *valPtr = val;
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
      if (menuState.editDepth == 7 - floatItemData[item - FIRST_FLOAT_ITEM].decimalPlaces())
        menuState.editDepth++;
    }

    if (menuState.editDepth > 7 || item >= FIRST_ACTION_ITEM)
      stopEditing();

    return;
  }

  if (item < FIRST_FLOAT_ITEM)
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
    menuState.firstItemMenuIndex = min(menuState.highlightedItemMenuIndex, menuData[menuState.currentMenu].itemCount() - 1);
    return;
  }

  if (item < FIRST_ACTION_ITEM)
  {
    // the setpoint flashes "Trip" if the unit has tripped; OK clears the trip
    if (tripped && item == ITEM_SETPOINT)
    {
      tripped = false;
      return;
    }
    // it's a numeric value: mark that the user wants to edit it
    // (the cursor will change if they can't)
    startEditing();
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
    setpoint = setPoints[setpointIndex];
    markSettingsDirty();

    // return to the prior menu
    backKeyPress();
    break;

  case ITEM_PID_MODE:
  case ITEM_TRIP_ENABLED:
  case ITEM_TRIP_AUTORESET:
    startEditing();
    break;

  case ITEM_PID_DIRECTION:
    if (modeIndex == AUTOMATIC)
      startEditing();
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

  default:
    BUGCHECK();
  }
}

// returns true if there was a long-press action; false if a long press
// is the same as a short press
static bool okKeyLongPress()
{
  byte item = menuData[menuState.currentMenu].itemAt(menuState.highlightedItemMenuIndex);

  // only two items respond to long presses: the setpoint and the profile menu
  switch (item)
  {
  case ITEM_SETPOINT:
    // open the setpoint menu
    menuState.currentMenu = ITEM_SETPOINT_MENU;
    break;
  case ITEM_PROFILE_MENU:
    menuState.currentMenu = ITEM_PROFILE_MENU;
    break;
  default:
    return false;
  }

  menuState.highlightedItemMenuIndex = 0;
  menuState.firstItemMenuIndex = 0;

  return true;
}

