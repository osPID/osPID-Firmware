/* This file contains the implementation of the on-screen menu system of the controller */

#include <avr/pgmspace.h>

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MENU"), __LINE__);

enum { TYPE_NAV = 0, TYPE_VAL = 1, TYPE_OPT = 2 };

enum {
  FLOAT_FLAG_1_DECIMAL_PLACE = 0x10,
  FLOAT_FLAG_2_DECIMAL_PLACES = 0,
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
    return (pgm_read_byte_near(&pmemFlags) & FLAG_1_DECIMAL_PLACE ? 1 : 2);
  }

  double minimumValue() const {
    return (pgm_read_byte_near(&pmemFlags) & FLAG_RANGE_0_99 ? 0 : -999.9);
  }

  double maximumValue() const {
    return (pgm_read_byte_near(&pmemFlags) & FLAG_RANGE_0_99 ? 99.99 : 999.9);
  }

  double currentValue() const {
    double *fp = (double *)pgm_read_word_near(&pmemFPtr);
    return *fp;
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

enum { MENU_FLAG_4x4_FORMAT = 0x01 };

struct MenuItem {
  byte pmemItemCount;
  byte pmemFlags;
  byte *pmemItemPtr;

  byte itemCount() const {
    return pgm_read_byte_near(&pmemItemCount);
  }

  byte item(byte index) const {
    return pgm_read_byte_near(&pmemItemPtr[index]);
  }

  bool is4x4() const {
    return (pgm_read_byte_near(&pmemFlags) & MENU_FLAG_4x4_FORMAT);
  }
};

// all of the items which might be displayed on the screen
enum {
  // all menus must be first
  ITEM_MAIN_MENU,
  ITEM_DASHBOARD_MENU,
  ITEM_CONFIG_MENU,
  ITEM_PROFILE_MENU,
  ITEM_SETPOINT_MENU,
  
  // then double items
  FIRST_FLOAT_ITEM,
  ITEM_SETPOINT = FIRST_FLOAT_ITEM,
  ITEM_INPUT,
  ITEM_OUTPUT,
  ITEM_KP,
  ITEM_KI,
  ITEM_KD,

  // then generic/specialized items
  FIRST_ACTION_ITEM,
  ITEM_AUTOTUNE_CMD = FIRST_ACTION_ITEM,
  ITEM_PROFILE1,
  ITEM_PROFILE2,
  ITEM_PROFILE3,
  ITEM_PROFILE4,
  ITEM_SETPOINT1,
  ITEM_SETPOINT2,
  ITEM_SETPOINT3,
  ITEM_SETPOINT4,
  ITEM_PID_MODE,
  ITEM_PID_DIRECTION,
  
  ITEM_COUNT,
  MENU_COUNT = FIRST_FLOAT_ITEM,
  FLOAT_ITEM_COUNT = FIRST_ACTION_ITEM - FIRST_FLOAT_ITEM
}

PROGMEM byte mainMenuItems[] = { ITEM_DASHBOARD_MENU, ITEM_CONFIG_MENU, ITEM_AUTOTUNE_CMD, ITEM_PROFILE_CMD };
PROGMEM byte dashMenuItems[] = { ITEM_SETPOINT, ITEM_INPUT, ITEM_OUTPUT, ITEM_PID_MODE };
PROGMEM byte configMenuItems[] = { ITEM_KP, ITEM_KI, ITEM_KD, ITEM_PID_DIRECTION };
PROGMEM byte profileMenuItems[] = { ITEM_PROFILE1, ITEM_PROFILE2, ITEM_PROFILE3, ITEM_PROFILE4 };
PROGMEM byte setpointMenuItems[] = { ITEM_SETPOINT1, ITEM_SETPOINT2, ITEM_SETPOINT3, ITEM_SETPOINT4 };

PROGMEM MenuItem menuData[MENU_COUNT] =
{
  { 4, 0, &mainMenuItems },
  { 4, 0, &dashMenuItems },
  { 4, 0, &configMenuItems },
  { 4, 0, &profileMenuItems },
  { 4, MENU_FLAG_4x4_FORMAT, &setpointMenuItems }
};

PROGMEM FloatItem floatItemData[FLOAT_ITEM_COUNT] =
{
  { 'S', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE, &setpoint },
  { 'I', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE | FLOAT_FLAG_NO_EDIT, &input },
  { 'O', FLOAT_FLAG_RANGE_M999_P999 | FLOAT_FLAG_1_DECIMAL_PLACE | FLOAT_FLAG_EDIT_MANUAL_ONLY, &output },
  { 'P', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &kp },
  { 'I', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &ki },
  { 'D', FLOAT_FLAG_RANGE_0_99 | FLOAT_FLAG_2_DECIMAL_PLACES, &kd },
};

struct MenuStateData {
  byte currentMenu;
  byte firstItemMenuIndex;
  byte highlightedItemMenuIndex;
  byte editDepth;
  bool editing;
};

struct MenuStateData menuState;

byte mMain[] = {
  0,1,2,3};
byte mDash[] = {
  4,5,6,7};
byte mConfig[] = {
  8,9,10,11};
byte *mMenu[] = {
  mMain, mDash, mConfig};

byte curMenu=0, mIndex=0, mDrawIndex=0;

bool editing=false;
byte editDepth=0;
byte highlightedIndex=0;

void drawLCD()
{
  boolean highlightFirst= (mDrawIndex==mIndex);
  drawItem(0,highlightFirst, mMenu[curMenu][mDrawIndex]);
  drawItem(1,!highlightFirst, mMenu[curMenu][mDrawIndex+1]);  
  if(editing) theLCD.setCursor(editDepth, highlightFirst?0:1);
}

// draw a floating-point item's value at the current position
void drawFloat(byte item)
{
  byte buffer[7];
  byte itemIndex = item - FIRST_FLOAT_ITEM;
  char icon = floatItemData[itemIndex].icon();
  double val = floatItemData[itemIndex].currentValue();
  byte decimals = floatItemData[itemIndex].decimalPlaces();

  if (isnan(val))
  {
    // display an error
    theLCD.print(icon);
    theLCD.print(now % 2048 < 1024 ? F(" Error"):F("      "));
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
  byte decimalPointPosition = 6-dec;

  for(byte i = 6; i >= 1; i--)
  {
    if (i == decimalPointPosition)
      buffer[i] = '.';
    else {
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
      else {
        buffer[i] = num % 10 + '0';
        num /= 10;
      }
    }
  }

  theLCD.print(buffer);
}

// draw the selector character at the current position
void drawSelector(byte item, bool selected)
{
  bool canEdit = !tuning;

  if (item < FIRST_FLOAT_ITEM)
    canEdit = true; // menus always get a '>' selector
  else if (item < FIRST_ACTION_ITEM)
    canEdit = canEdit && floatItemData[item - FIRST_FLOAT_ITEM].canEdit();

  if (!selected)
    theLCD.print(' ');
  else if (menuState.editing)
    theLCD.print(canEdit ? '[' : '|');
  else
    theLCD.print(canEdit ? '>' : '}');
}

// draw a profile name at the current position
void drawProfileName(byte profileIndex)
{
  for (byte i = 0; i < 8; i++)
    theLCD.print(getProfileNameCharAt(profileIndex, i));
}

// draw an item occupying a full 8x1 display line
void drawFullLineItem(byte row, bool selected, byte item)
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
  case ITEM_AUTOTUNE_CMD:
      theLCD.print(tuning ? F("Cancel ") : F("AutoTun"));
      break;
  case ITEM_PROFILE1:
  case ITEM_PROFILE2:
  case ITEM_PROFILE3:
  case ITEM_PROFILE4:
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
  default:
    BUGCHECK();
  }
}

// flash a status indicator if appropriate
void drawStatusFlash()
{
  if(tuning)
  {
    if (now % 2048 < 700)
    {
      theLCD.setCursor(0,row);
      theLCD.print('T');
    }
  }
  else if (runningProfile)
  {
    if (now % 2048 < 700)
    {
      theLCD.setCursor(0,row);
      theLCD.print('P');
    }
    else if (now % 2048 < 1400)
    {
      theLCD.setCursor(0,row);
      char c;
      if (curProfStep < 10)
        c = curProfStep + '0';
      else c = curProfStep + 'A';
      theLCD.print(c);
    }
  }
}

void drawHalfRowItem(byte row, byte col, bool selected, byte item)
{
  theLCD.setCursor(col, row);

  drawSelector(item, selected);

  switch (item) {
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

void drawItem(byte row, boolean highlight, byte index)
{
  char buffer[7];
  theLCD.setCursor(0,row);
  double val=0;
  byte dec=0;
  int num=0;
  char icon=' ';
  boolean isNeg = false;
  boolean didneg = false;
  byte decSpot = 0;
  boolean edit = editing && highlightedIndex==index;
  boolean canEdit=!tuning;
  switch(getMenuType(index))
  {
  case TYPE_NAV:
    theLCD.print(highlight? '>':' ');
    switch(index)
    {
    case 0: 
      theLCD.print(F("DashBrd")); 
      break;
    case 1: 
      theLCD.print(F("Config ")); 
      break;
    case 2: 
      theLCD.print(tuning ? F("Cancel ") : F("ATune  ")); 
      break;
    case 3:
      if(runningProfile)theLCD.print(F("Cancel "));
      else theLCD.print(profname);
      break;
    default: 
      return;
    }

    break;
  case TYPE_VAL:

    switch(index)
    {
    case 4: 
      val = setpoint; 
      dec=1; 
      icon='S'; 
      break;
    case 5: 
      val = input; 
      dec=1; 
      icon='I'; 
      canEdit=false;
      break;
    case 6: 
      val = output; 
      dec=1; 
      icon='O'; 
      canEdit = (modeIndex==0);
      break;
    case 8: 
      val = kp; 
      dec=2; 
      icon='P'; 
      break;
    case 9: 
      val = ki; 
      dec=2; 
      icon='I'; 
      break ;
    case 10: 
      val = kd; 
      dec=2; 
      icon='D'; 
      break ;

    default: 
      return;
    }
    theLCD.print(edit? '[' : (highlight ? (canEdit ? '>':'|') : 
    ' '));

    if(isnan(val))
    { //display an error
      theLCD.print(icon);
      theLCD.print( now % 2000<1000 ? F(" Error"):F("      ")); 
      return;
    }

    for(int i=0;i<dec;i++) val*=10;

    num = (int)round(val);
    buffer[0] = icon;
    isNeg = num<0;
    if(isNeg) num = 0 - num;
    didneg = false;
    decSpot = 6-dec;
    if(decSpot==6)decSpot=7;
    for(byte i=6; i>=1;i--)
    {
      if(i==decSpot)buffer[i] = '.';
      else {
        if(num==0)
        {
          if(i>=decSpot-1) buffer[i]='0';
          else if (isNeg && !didneg)
          {
            buffer[i]='-';
            didneg=true;
          }
          else buffer[i]=' ';
        }
        else {
          buffer[i] = num%10+48;
          num/=10;
        }
      }
    }     
    theLCD.print(buffer);
    break;
  case TYPE_OPT: 

    theLCD.print(edit ? '[': (highlight? '>':' '));    
    switch(index)
    {
    case 7:    
      theLCD.print(modeIndex==0 ? F("M Man  "):F("M Auto ")); 
      break;
    case 11://12: 

      theLCD.print(ctrlDirection==0 ? F("A Direc"):F("A Rever")); 
      break;
    }

    break;
  default: 
    return;
  }

  //indication of altered state
  if(highlight && (tuning || runningProfile))
  {
    //should we blip?
    if(tuning)
    { 
      if(now % 1500 <500)
      {
        theLCD.setCursor(0,row);
        theLCD.print('T'); 
      }
    }
    else //running profile
    {
      if(now % 2000 < 500)
      {
        theLCD.setCursor(0,row);
        theLCD.print('P');
      }
      else if(now%2000 < 1000)
      {
        theLCD.setCursor(0,row);
        char c;
        if(curProfStep<10) c = curProfStep + 48; //0-9
        else c = curProfStep + 65; //A,B...
        theLCD.print(c);      
      }  
    }
  }
}

byte getValDec(byte index)
{       
  switch(index)
  {
  case 4: 
  case 5: 
  case 6: 
  //case 11: 
    return 1;
  case 8: 
  case 9: 
  case 10: 
  default:
    return 2;
  }
}
byte getMenuType(byte index)
{
  switch(index)
  {
  case 0:
  case 1:
  case 2:
  case 3:
    return TYPE_NAV;
  case 4: 
  case 5: 
  case 6: 
  case 8: 
  case 9: 
  case 10: 
  //case 11:
    return TYPE_VAL;
  case 7:
  case 11: //12:
    return TYPE_OPT;
  default: 
    return 255;
  }
}

boolean changeflag=false;

void back()
{
  if(editing)
  { //decrease the depth and stop editing if required

    editDepth--;
    if(getMenuType(highlightedIndex)==TYPE_VAL)
    {
      if(editDepth==7-getValDec(highlightedIndex))editDepth--; //skip the decimal  
    }
    if(editDepth<3)
    {
      editDepth=0;
      editing= false;
      theLCD.noCursor();
    }
  }
  else
  { //if not editing return to previous menu. currently this is always main

    //depending on which menu we're coming back from, we may need to write to the eeprom
    if(changeflag)
    {
      if(curMenu==1)
      { 
        saveEEPROMSettings();
      }
      else if(curMenu==2) //tunings may have changed
      {
        saveEEPROMSettings();
        myPID.SetTunings(kp,ki,kd);
        myPID.SetControllerDirection(ctrlDirection);
      }
      changeflag=false;
    }
    if(curMenu!=0)
    { 
      highlightedIndex = curMenu-1; //make sure the arrow is on the menu they were in
      mIndex=curMenu-1;
      curMenu=0;
      mDrawIndex=0;

    }
  }
}

double getValMin(byte index)
{
  switch(index)
  {
  case 4: 
  case 5: 
  case 6: 
//  case 11: 
    return -999.9;
  case 8: 
  case 9: 
  case 10: 
  default:
    return 0;
  }
}

double getValMax(byte index)
{
  switch(index)
  {
  case 4: 
  case 5: 
  case 6: 
  //case 11: 
    return 999.9;
  case 8: 
  case 9: 
  case 10: 
  default:
    return 99.99;
  } 

}

void updown(bool up)
{

  if(editing)
  {
    changeflag = true;
    byte decdepth;
    double adder;
    switch(getMenuType(highlightedIndex))
    {
    case TYPE_VAL:
      decdepth = 7 - getValDec(highlightedIndex);
      adder=1;
      if(editDepth<decdepth-1)for(int i=editDepth;i<decdepth-1;i++)adder*=10;
      else if(editDepth>decdepth)for(int i=decdepth;i<editDepth;i++)adder/=10;

      if(!up)adder = 0-adder;

      double *val, minimum, maximum;
      switch(highlightedIndex)
      {
      case 4: 
        val=&setpoint; 
        break;
      case 6:  
        val=&output; 
        break;
      case 8:  
        val=&kp; 
        break;
      case 9:  
        val=&ki; 
        break;
      case 10:  
        val=&kd; 
        break;
      }

      minimum = getValMin(highlightedIndex);
      maximum = getValMax(highlightedIndex);
      (*val)+=adder;
      if((*val)>maximum)(*val)=maximum;
      else if((*val)<minimum)(*val)=minimum;
      break; 
    case TYPE_OPT:
      switch(highlightedIndex)
      {
      case 7:
        modeIndex= (modeIndex==0?1:0);
        /*mode change code*/
        myPID.SetMode(modeIndex);
        break;
      case 11://12:
        ctrlDirection = (ctrlDirection==0?1:0); 
        Serial.println(ctrlDirection);
        break;
      }

      break;
    }

  }
  else
  {
    if(up)
    {
      if(mIndex>0)
      {
        mIndex--;
        mDrawIndex=mIndex;
      }
    }
    else
    {
      byte limit = 3;// (curMenu==2 ? 4 : 3); 
      if(mIndex<limit)
      {
        mDrawIndex =mIndex;
        mIndex++;
      }
    }
    highlightedIndex = mMenu[curMenu][mIndex];
  }
}

void ok()
{
  if(editing)
  {
    byte dec = getValDec(highlightedIndex);
    if(getMenuType(highlightedIndex) == TYPE_VAL &&(editDepth<6 || (editDepth==6 && dec!=1)))
    {
      editDepth++;
      if(editDepth==7-dec)editDepth++; //skip the decimal
    }
  }
  else
  {

    switch(highlightedIndex)
    {
    case 0: 
      curMenu=1;
      mDrawIndex=0;
      mIndex=0; 
      highlightedIndex = 4; //setpoint
      changeflag = false;
      break;
    case 1: 
      curMenu=2;
      mDrawIndex=0;
      mIndex=0; 
      highlightedIndex = 8; //kp
      changeflag = false;
      break;
    case 2: 
      changeAutoTune();/*autotune code*/
      break;
    case 3: 
      if(runningProfile)StopProfile();
      else StartProfile();

      break;
    case 5: /*nothing for input*/
      break;
    case 6: 
      if(modeIndex==0 && !tuning) editing=true; 
      break; //output
    case 4: //setpoint
    case 8: //kp
    case 9: //ki
    case 10: //kd
//    case 11: //windowsize
    case 11: //12: //direction
      editing=true;
      break; //verify this is correct
    case 7: 
      if(!tuning) editing=true; 
      break; //mode
    }
    if(editing)
    {
      editDepth=3;
      theLCD.cursor();
    }
  }
}

