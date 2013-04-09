/* This file contains the implementation of the on-screen menu system of the controller */

#include <avr/pgmspace.h>

#undef BUGCHECK
#define BUGCHECK() ospBugCheck(PSTR("MENU"), __LINE__);

enum { TYPE_NAV = 0, TYPE_VAL = 1, TYPE_OPT = 2 };


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

const char * autotuneGuiCmd(GuiItem::CommandFnAction req)
{
  return "";
}

const char * profileGuiCmd(GuiItem::CommandFnAction req)
{
  return "";
}

