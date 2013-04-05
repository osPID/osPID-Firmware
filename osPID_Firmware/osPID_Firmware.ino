#include <LiquidCrystal.h>
#include <Arduino.h>
#include "AnalogButton_local.h"
#include "PID_v1_local.h"
#include "PID_AutoTune_v0_local.h"
#include "ospSettingsHelper.h"
#include "ospCardSimulator.h"
#include "ospTemperatureInputCard.h"
#include "ospDigitalOutputCard.h"

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

// Pin assignments on the controller card (_not_ the I/O cards)
enum { buzzerPin = 3, systemLEDPin = A2 };

const byte TYPE_NAV=0;
const byte TYPE_VAL=1;
const byte TYPE_OPT=2;

byte mMain[] = {
  0,1,2,3};
byte mDash[] = {
  4,5,6,7};
byte mConfig[] = {
  8,9,10,11};
byte *mMenu[] = {
  mMain, mDash, mConfig};

byte curMenu=0, mIndex=0, mDrawIndex=0;
LiquidCrystal lcd(A1, A0, 4, 7, 8, 9);
AnalogButton button(A3, 0, 253, 454, 657);

unsigned long now, lcdTime, buttonTime,ioTime, serialTime;
boolean sendInfo=true, sendDash=true, sendTune=true, sendInputConfig=true, sendOutputConfig=true;

bool editing=false;

bool tuning = false;

double setpoint=250,input=250,output=50, pidInput=250;

double kp = 2, ki = 0.5, kd = 2;
byte ctrlDirection = 0;
byte modeIndex = 0;
byte highlightedIndex=0;

PID myPID(&pidInput, &output, &setpoint,kp,ki,kd, DIRECT);

double aTuneStep = 20, aTuneNoise = 1;
unsigned int aTuneLookBack = 10;
byte ATuneModeRemember = 0;
PID_ATune aTune(&pidInput, &output);


byte curProfStep=0;
byte curType=0;
float curVal=0;
float helperVal=0;
unsigned long helperTime=0;
boolean helperflag=false;
unsigned long curTime=0;


/*Profile declarations*/
const unsigned long profReceiveTimeout = 10000;
unsigned long profReceiveStart=0;
boolean receivingProfile=false;
const int nProfSteps = 15;
char profname[] = {
  'N','o',' ','P','r','o','f'};
byte proftypes[nProfSteps];
unsigned long proftimes[nProfSteps];
float profvals[nProfSteps];
boolean runningProfile = false;

void setup()
{
  Serial.begin(9600);
  lcdTime=10;
  buttonTime=1;
  ioTime=5;
  serialTime=6;
  //windowStartTime=2;
  lcd.begin(8, 2);

  lcd.setCursor(0,0);
  lcd.print(F(" osPID   "));
  lcd.setCursor(0,1);
  lcd.print(F(" v1.60   "));
  delay(1000);

  initializeEEPROM();

  theInputCard.initialize();
  theOutputCard.initialize();

  myPID.SetSampleTime(1000);
  myPID.SetOutputLimits(0, 100);
  myPID.SetTunings(kp, ki, kd);
  myPID.SetControllerDirection(ctrlDirection);
  myPID.SetMode(modeIndex);
}

byte editDepth=0;
void loop()
{
  now = millis();

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
      updown(true);
      break;

    case BUTTON_DOWN:
      updown(false);
      break;

    case BUTTON_OK:
      ok();
      break;
    }
    buttonTime += 50;
  }

  bool doIO = now >= ioTime;
  //read in the input
  if(doIO)
  { 
    ioTime+=250;
    input = theInputCard.readInput();
    if (!isnan(input))pidInput = input;
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
  else
  {
    if(runningProfile) ProfileRunTime();
    //allow the pid to compute if necessary
    myPID.Compute();
  }





  if(doIO)
  {
    theOutputCard.setOutputPercent(output);
  }

  if(now>lcdTime)
  {
    drawLCD();
    lcdTime+=250; 
  }
  if(millis() > serialTime)
  {
    //if(receivingProfile && (now-profReceiveStart)>profReceiveTimeout) receivingProfile = false;
    SerialReceive();
    SerialSend();
    serialTime += 500;
  }
}


void drawLCD()
{
  boolean highlightFirst= (mDrawIndex==mIndex);
  drawItem(0,highlightFirst, mMenu[curMenu][mDrawIndex]);
  drawItem(1,!highlightFirst, mMenu[curMenu][mDrawIndex+1]);  
  if(editing) lcd.setCursor(editDepth, highlightFirst?0:1);
}

void drawItem(byte row, boolean highlight, byte index)
{
  char buffer[7];
  lcd.setCursor(0,row);
  double val=0;
  int dec=0;
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
    lcd.print(highlight? '>':' ');
    switch(index)
    {
    case 0: 
      lcd.print(F("DashBrd")); 
      break;
    case 1: 
      lcd.print(F("Config ")); 
      break;
    case 2: 
      lcd.print(tuning ? F("Cancel ") : F("ATune  ")); 
      break;
    case 3:
      if(runningProfile)lcd.print(F("Cancel "));
      else lcd.print(profname);
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
    lcd.print(edit? '[' : (highlight ? (canEdit ? '>':'|') : 
    ' '));
    
    if(isnan(val))
    { //display an error
      lcd.print(icon);
      lcd.print( now % 2000<1000 ? F(" Error"):F("      ")); 
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
    lcd.print(buffer);
    break;
  case TYPE_OPT: 

    lcd.print(edit ? '[': (highlight? '>':' '));    
    switch(index)
    {
    case 7:    
      lcd.print(modeIndex==0 ? F("M Man  "):F("M Auto ")); 
      break;
    case 11://12: 

      lcd.print(ctrlDirection==0 ? F("A Direc"):F("A Rever")); 
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
        lcd.setCursor(0,row);
        lcd.print('T'); 
      }
    }
    else //running profile
    {
      if(now % 2000 < 500)
      {
        lcd.setCursor(0,row);
        lcd.print('P');
      }
      else if(now%2000 < 1000)
      {
        lcd.setCursor(0,row);
        char c;
        if(curProfStep<10) c = curProfStep + 48; //0-9
        else c = curProfStep + 65; //A,B...
        lcd.print(c);      
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
      lcd.noCursor();
    }
  }
  else
  { //if not editing return to previous menu. currently this is always main


    //depending on which menu we're coming back from, we may need to write to the eeprom
    if(changeflag)
    {
      if(curMenu==1)
      { 
        EEPROMBackupDash();
      }
      else if(curMenu==2) //tunings may have changed
      {
        EEPROMBackupTunings();
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
      lcd.cursor();
    }
  }
}

void changeAutoTune()
{
  if(!tuning)
  {
    //initiate autotune
    AutoTuneHelper(true);
    aTune.SetNoiseBand(aTuneNoise);
    aTune.SetOutputStep(aTuneStep);
    aTune.SetLookbackSec((int)aTuneLookBack);
    tuning = true;
  }
  else
  { //cancel autotune
    aTune.Cancel();
    tuning = false;
    AutoTuneHelper(false);
  }
}

void AutoTuneHelper(boolean start)
{

  if(start)
  {
    ATuneModeRemember = myPID.GetMode();
    myPID.SetMode(MANUAL);
  }
  else
  {
    modeIndex = ATuneModeRemember;
    myPID.SetMode(modeIndex);
  } 
}






void StartProfile()
{
  if(!runningProfile)
  {
    //initialize profle
    curProfStep=0;
    runningProfile = true;
    calcNextProf();
  }
}
void StopProfile()
{
  if(runningProfile)
  {
    curProfStep=nProfSteps;
    calcNextProf(); //runningProfile will be set to false in here
  } 
}


void ProfileRunTime()
{
  if(tuning || !runningProfile) return;
  


  boolean gotonext = false;

  //what are we doing?
  if(curType==1) //ramp
  {
    //determine the value of the setpoint
    if(now>helperTime)
    {
      setpoint = curVal;
      gotonext=true;
    }
    else
    {
      setpoint = (curVal-helperVal)*(1-(float)(helperTime-now)/(float)(curTime))+helperVal; 
    }
  }
  else if (curType==2) //wait
  {
    float err = input-setpoint;
    if(helperflag) //we're just looking for a cross
    {

      if(err==0 || (err>0 && helperVal<0) || (err<0 && helperVal>0)) gotonext=true;
      else helperVal = err;
    }
    else //value needs to be within the band for the perscribed time
    {
      if (abs(err)>curVal) helperTime=now; //reset the clock
      else if( (now-helperTime)>=curTime) gotonext=true; //we held for long enough
    }

  }
  else if(curType==3) //step
  {

    if((now-helperTime)>curTime)gotonext=true;
  }
  else if(curType==127) //buzz
  {
    if(now<helperTime)digitalWrite(buzzerPin,HIGH);
    else 
    {
       digitalWrite(buzzerPin,LOW);
       gotonext=true;
    }
  }
  else
  { //unrecognized type, kill the profile
    curProfStep=nProfSteps;
    gotonext=true;
  }





  if(gotonext)
  {
    curProfStep++;
    calcNextProf();
  }
}

void calcNextProf()
{
  if(curProfStep>=nProfSteps) 
  {
    curType=0;
    helperTime =0;
  }
  else
  { 
    curType = proftypes[curProfStep];
    curVal = profvals[curProfStep];
    curTime = proftimes[curProfStep];

  }
  if(curType==1) //ramp
  {
    helperTime = curTime + now; //at what time the ramp will end
    helperVal = setpoint;
  }   
  else if(curType==2) //wait
  {
    helperflag = (curVal==0);
    if(helperflag) helperVal= input-setpoint;
    else helperTime=now; 
  }
  else if(curType==3) //step
  {
    setpoint = curVal;
    helperTime = now;
  }
  else if(curType==127) //buzzer
  {
    helperTime = now + curTime;    
  }
  else
  {
    curType=0;
  }



  if(curType==0) //end
  { //we're done 
    runningProfile=false;
    curProfStep=0;
    Serial.println("P_DN");
    digitalWrite(buzzerPin,LOW);
  } 
  else
  {
    Serial.print("P_STP ");
    Serial.print(int(curProfStep));
    Serial.print(" ");
    Serial.print(int(curType));
    Serial.print(" ");
    Serial.print((curVal));
    Serial.print(" ");
    Serial.println((curTime));
  }

}

/********************************************
 * Serial Communication functions / helpers
 ********************************************/

boolean ackDash = false, ackTune = false;
union {                // This Data structure lets
  byte asBytes[32];    // us take the byte array
  float asFloat[8];    // sent from processing and
}                      // easily convert it to a
foo;                   // float array

// getting float values from processing into the arduino
// was no small task.  the way this program does it is
// as follows:
//  * a float takes up 4 bytes.  in processing, convert
//    the array of floats we want to send, into an array
//    of bytes.
//  * send the bytes to the arduino
//  * use a data structure known as a union to convert
//    the array of bytes back into an array of floats
void SerialReceive()
{

  // read the bytes sent from Processing
  byte index=0;
  byte identifier=0;
  byte b1=255,b2=255;
  boolean boolhelp=false;

  while(Serial.available())
  {
    byte val = Serial.read();
    if(index==0){ 
      identifier = val;
      Serial.println(int(val));
    }
    else 
    {
      switch(identifier)
      {
      case 0: //information request 
        if(index==1) b1=val; //which info type
        else if(index==2)boolhelp = (val==1); //on or off
        break;
      case 1: //dasboard
      case 2: //tunings
      case 3: //autotune
        if(index==1) b1 = val;
        else if(index<14)foo.asBytes[index-2] = val; 
        break;
      case 4: //EEPROM reset
        if(index==1) b1 = val; 
        break;
      case 5: /*//input configuration
        if (index==1)InputSerialReceiveStart();
        InputSerialReceiveDuring(val, index);
        break;
      case 6: //output configuration
        if (index==1)OutputSerialReceiveStart();
        OutputSerialReceiveDuring(val, index);
        break;*/
      case 7:  //receiving profile
        if(index==1) b1=val;
        else if(b1>=nProfSteps) profname[index-2] = char(val); 
        else if(index==2) proftypes[b1] = val;
        else foo.asBytes[index-3] = val;

        break;
      case 8: //profile command
        if(index==1) b2=val;
        break;
      default:
        break;
      }
    }
    index++;
  }

  //we've received the information, time to act
  switch(identifier)
  {
  case 0: //information request
    switch(b1)
    {
    case 0:
      sendInfo = true; 
      sendInputConfig=true;
      sendOutputConfig=true;
      break;
    case 1: 
      sendDash = boolhelp;
      break;
    case 2: 
      sendTune = boolhelp;
      break;
    case 3: 
      sendInputConfig = boolhelp;
      break;
    case 4: 
      sendOutputConfig = boolhelp;
      break;
    default: 
      break;
    }
    break;
  case 1: //dashboard
    if(index==14  && b1<2)
    {
      setpoint=double(foo.asFloat[0]);
      //Input=double(foo.asFloat[1]);       // * the user has the ability to send the 
      //   value of "Input"  in most cases (as 
      //   in this one) this is not needed.
      if(b1==0)                       // * only change the output if we are in 
      {                                     //   manual mode.  otherwise we'll get an
        output=double(foo.asFloat[2]);      //   output blip, then the controller will 
      }                                     //   overwrite.

      if(b1==0) myPID.SetMode(MANUAL);// * set the controller mode
      else myPID.SetMode(AUTOMATIC);             //
      EEPROMBackupDash();
      ackDash=true;
    }
    break;
  case 2: //Tune
    if(index==14 && (b1<=1))
    {
      // * read in and set the controller tunings
      kp = double(foo.asFloat[0]);           //
      ki = double(foo.asFloat[1]);           //
      kd = double(foo.asFloat[2]);           //
      ctrlDirection = b1;
      myPID.SetTunings(kp, ki, kd);            //    
      if(b1==0) myPID.SetControllerDirection(DIRECT);// * set the controller Direction
      else myPID.SetControllerDirection(REVERSE);          //
      EEPROMBackupTunings();
      ackTune = true;
    }
    break;
  case 3: //ATune
    if(index==14 && (b1<=1))
    {

      aTuneStep = foo.asFloat[0];
      aTuneNoise = foo.asFloat[1];    
      aTuneLookBack = (unsigned int)foo.asFloat[2];
      if((!tuning && b1==1)||(tuning && b1==0))
      { //toggle autotune state
        changeAutoTune();
      }
      EEPROMBackupATune();
      ackTune = true;   
    }
    break;
  case 4: //EEPROM reset
    if(index==2 && b1<2) clearEEPROM();
    break;
  case 5: /*//input configuration
    InputSerialReceiveAfter(eepromInputOffset);
    sendInputConfig=true;
    break;
  case 6: //ouput configuration
    OutputSerialReceiveAfter(eepromOutputOffset);
    sendOutputConfig=true;
    break;*/
  case 7: //receiving profile

    if((index==11 || (b1>=nProfSteps && index==9) ))
    {
      if(!receivingProfile && b1!=0)
      { //there was a timeout issue.  reset this transfer
        receivingProfile=false;
        Serial.println("ProfError");
        EEPROMRestoreProfile();
      }
      else if(receivingProfile || b1==0)
      {
        if(runningProfile)
        { //stop the current profile execution
          StopProfile();
        }
          
        if(b1==0)
        {
          receivingProfile = true;
          profReceiveStart = millis();
        }

        if(b1>=nProfSteps)
        { //getting the name is the last step
          receivingProfile=false; //last profile step
          Serial.print("ProfDone ");
          Serial.println(profname);
          EEPROMBackupProfile();
          Serial.println("Archived");
        }
        else
        {
          profvals[b1] = foo.asFloat[0];
          proftimes[b1] = (unsigned long)(foo.asFloat[1] * 1000);
          Serial.print("ProfAck ");
          Serial.print(b1);           
          Serial.print(" ");
          Serial.print(proftypes[b1]);           
          Serial.print(" ");
          Serial.print(profvals[b1]);           
          Serial.print(" ");
          Serial.println(proftimes[b1]);           
        }
      }
    }
    break;
  case 8:
    if(index==2 && b2<2)
    {
      if(b2==1) StartProfile();
      else StopProfile();

    }
    break;
  default: 
    break;
  }
}


// unlike our tiny microprocessor, the processing ap
// has no problem converting strings into floats, so
// we can just send strings.  much easier than getting
// floats from processing to here no?
void SerialSend()
{
  if(sendInfo)
  {//just send out the stock identifier
    Serial.print("\nosPID v1.50");
    Serial.print(' ');
    Serial.print(theInputCard.cardIdentifier());
    Serial.print(' ');
    Serial.print(theOutputCard.cardIdentifier());
    Serial.println("");
    sendInfo = false; //only need to send this info once per request
  }
  if(sendDash)
  {
    Serial.print("DASH ");
    Serial.print(setpoint); 
    Serial.print(" ");
    if(isnan(input)) Serial.print("Error");
    else Serial.print(input); 
    Serial.print(" ");
    Serial.print(output); 
    Serial.print(" ");
    Serial.print(myPID.GetMode());
    Serial.print(" ");
    Serial.println(ackDash?1:0);
    if(ackDash)ackDash=false;
  }
  if(sendTune)
  {
    Serial.print("TUNE ");
    Serial.print(myPID.GetKp()); 
    Serial.print(" ");
    Serial.print(myPID.GetKi()); 
    Serial.print(" ");
    Serial.print(myPID.GetKd()); 
    Serial.print(" ");
    Serial.print(myPID.GetDirection()); 
    Serial.print(" ");
    Serial.print(tuning?1:0);
    Serial.print(" ");
    Serial.print(aTuneStep); 
    Serial.print(" ");
    Serial.print(aTuneNoise); 
    Serial.print(" ");
    Serial.print(aTuneLookBack); 
    Serial.print(" ");
    Serial.println(ackTune?1:0);
    if(ackTune)ackTune=false;
  }/*
  if(sendInputConfig)
  {
    Serial.print("IPT ");
    InputSerialSend();
    sendInputConfig=false;
  }
  if(sendOutputConfig)
  {
    Serial.print("OPT ");
    OutputSerialSend();
    sendOutputConfig=false;
  }*/
  if(runningProfile)
  {
    Serial.print("PROF ");
    Serial.print(int(curProfStep));
    Serial.print(" ");
    Serial.print(int(curType));
    Serial.print(" ");
switch(curType)
{
  case 1: //ramp
    Serial.println((helperTime-now)); //time remaining
     
  break;
  case 2: //wait
    Serial.print(abs(input-setpoint));
    Serial.print(" ");
    Serial.println(curVal==0? -1 : float(now-helperTime));
  break;  
  case 3: //step
    Serial.println(curTime-(now-helperTime));
  break;
  default: 
  break;
  
}

  }
  
}









