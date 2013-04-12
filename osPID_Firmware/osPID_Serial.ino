/* This file contains the routines implementing serial-port (i.e. USB) communications
   between the controller and a command computer */

boolean sendInfo=true, sendDash=true, sendTune=true, sendInputConfig=true, sendOutputConfig=true;

/*Profile declarations*/
const unsigned long profReceiveTimeout = 10000;
unsigned long profReceiveStart=0;
boolean receivingProfile=false;
const int nProfSteps = 15;
char profname[] = {
  'N','o',' ','P','r','o','f'};
byte proftypes[nProfSteps];
unsigned long proftimes[nProfSteps];
double profvals[nProfSteps];

// FIXME: configurable serial parameters?

boolean ackDash = false, ackTune = false;
union {                // This Data structure lets
  byte asBytes[32];    // us take the byte array
  double asFloat[8];    // sent from processing and
}                      // easily convert it to a
foo;                   // double array

// getting double values from processing into the arduino
// was no small task.  the way this program does it is
// as follows:
//  * a double takes up 4 bytes.  in processing, convert
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
      saveEEPROMSettings();
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
      saveEEPROMSettings();
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
        if (b1)
          startAutoTune();
        else
          stopAutoTune();
      }
      saveEEPROMSettings();
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
//        EEPROMRestoreProfile();
      }
      else if(receivingProfile || b1==0)
      {
        if(runningProfile)
        { //stop the current profile execution
          stopProfile();
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
          saveEEPROMProfile(0);
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
      if(b2==1) startProfile();
      else stopProfile();

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
    Serial.print(int(0));
    Serial.print(" ");
    Serial.print(int(0));
    Serial.print(" ");
switch(1)
{
  case 1: //ramp
    Serial.println((now)); //time remaining

  break;
  case 2: //wait
    Serial.print(abs(input-setpoint));
    Serial.print(" ");
    Serial.println(0==0? -1 : double(now-0));
  break;  
  case 3: //step
    Serial.println(0-(now-0));
  break;
  default: 
  break;

}

  }

}

