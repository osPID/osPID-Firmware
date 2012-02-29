/********************************************
 * Serial Communication functions / helpers
 ********************************************/


union {                // This Data structure lets
  byte asBytes[24];    // us take the byte array
  float asFloat[6];    // sent from processing and
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
  int index=0;
  byte Auto_Man = -1;
  byte Direct_Reverse = -1;
  byte ATune = -1;
  byte resetFlag=-1;
  byte iType=-1, oType=-1;
  int identifier=0;
  
  
  while(Serial.available())
  {
    byte val = Serial.read();
    if(index==0) identifier = val;
    else if(identifier==1) //general
    {
      if(index==1) Auto_Man = val;
      else if (index<14) foo.asBytes[index-2] = val;
    }
    else if(identifier==2)//Tunings
    {
       if(index==1) Direct_Reverse = val;
       else if(index<14)foo.asBytes[index-2] = val; 
    }
    else if(identifier==3)//Auto Tune
    {
       if(index==1) ATune = val;
       else if(index<14)foo.asBytes[index-2] = val; 
    }
    else if(identifier==4)//EEPROM reset
    {
       if(index==1) resetFlag = val; 
    }
    else if(identifier==5) //config set
    {
        if(index==1) iType = val;
        else if(index==2) oType= val;
        else if(index<19) foo.asBytes[index-3] = val;
    }
    index++;
  } 
  
  // if the information we got was in the correct format, 
  // read it into the system
  if(identifier==1 && index==14  && (Auto_Man==0 || Auto_Man==1))
  {
    setpoint=double(foo.asFloat[0]);
    //Input=double(foo.asFloat[1]);       // * the user has the ability to send the 
                                          //   value of "Input"  in most cases (as 
                                          //   in this one) this is not needed.
    if(Auto_Man==0)                       // * only change the output if we are in 
    {                                     //   manual mode.  otherwise we'll get an
      output=double(foo.asFloat[2]);      //   output blip, then the controller will 
    }                                     //   overwrite.
       
    if(Auto_Man==0) myPID.SetMode(MANUAL);// * set the controller mode
    else myPID.SetMode(AUTOMATIC);             //
    
    EEPROMBackupDash();
    
    serialAcknowledge=1;
    
  }
  else if(identifier==2 && index==14 && (Direct_Reverse==0 || Direct_Reverse==1))
  {
                                           // * read in and set the controller tunings
    kp = double(foo.asFloat[0]);           //
    ki = double(foo.asFloat[1]);           //
    kd = double(foo.asFloat[2]);           //
    ctrlDirection = Direct_Reverse;
    myPID.SetTunings(kp, ki, kd);            //    
    if(Direct_Reverse==0) myPID.SetControllerDirection(DIRECT);// * set the controller Direction
    else myPID.SetControllerDirection(REVERSE);          //
    EEPROMBackupTunings();
    serialAcknowledge=2;
  }
  else if(identifier==3 && index==14 && (ATune==0 || ATune==1))
  {

    aTuneStep = foo.asFloat[0];
    aTuneNoise = foo.asFloat[1];    
    aTuneLookBack = (unsigned int)foo.asFloat[2];
    if((!tuning && ATune==1)||(tuning && ATune==0))
    { //toggle autotune state
      changeAutoTune(0);
    }
    serialAcknowledge=3;    
  }
  else if(identifier==4 && index==2 && (resetFlag==0||resetFlag==1))
  {
    EEPROMreset();
    serialAcknowledge=4;
  }
  else if(identifier==5 && index==19 && (iType==0 || iType==1 || iType==2) && (oType==0 || oType==1))
  {
    inputType = iType;
    if(outputType != oType)
    {
      if (oType==0)digitalWrite(SSRPin, LOW);
      else if(oType==1) digitalWrite( RelayPin,LOW); //turn off the other pin
       outputType=oType; 
    }
    digitalWrite(2, (inputType==2 ? true : false));
    THERMISTORNOMINAL = foo.asFloat[0];
    BCOEFFICIENT = foo.asFloat[1];
    TEMPERATURENOMINAL = foo.asFloat[2];
    outWindowSec =  foo.asFloat[3];
    EEPROMBackupIOParams();
    setOutputWindow(outWindowSec);
    serialAcknowledge=5;
  }
  
  
  Serial.flush();                         // * clear any random data from the serial buffer
}

// unlike our tiny microprocessor, the processing ap
// has no problem converting strings into floats, so
// we can just send strings.  much easier than getting
// floats from processing to here no?

union {                // This Data structure lets
  byte asBytes[36];    // us take the byte array
  float asFloat[9];    // sent from processing and
}                      // easily convert it to a
sender;   

void SerialSend()
{
  Serial.print("PID1 ");
  Serial.print(setpoint); Serial.print(" ");
  Serial.print(input); Serial.print(" ");
  Serial.print(output); Serial.print(" ");
  Serial.print(myPID.GetKp()); Serial.print(" ");
  Serial.print(myPID.GetKi()); Serial.print(" ");
  Serial.print(myPID.GetKd()); Serial.print(" ");
  Serial.print(aTuneStep); Serial.print(" ");
  Serial.print(aTuneNoise); Serial.print(" ");
  Serial.print(aTuneLookBack); Serial.print(" ");  
  Serial.print(THERMISTORNOMINAL); Serial.print(" ");  
  Serial.print(BCOEFFICIENT); Serial.print(" ");  
  Serial.print(TEMPERATURENOMINAL); Serial.print(" ");  
  Serial.print(outWindowSec); Serial.print(" ");  
  Serial.print((int)inputType); Serial.print(" ");  
  Serial.print((int)outputType); Serial.print(" ");
  Serial.print(myPID.GetMode()); Serial.print(" ");
  Serial.print(myPID.GetDirection()); Serial.print(" ");
  Serial.print(tuning?1:0); Serial.print(" ");
  Serial.println((int)serialAcknowledge);  
  serialAcknowledge=0;
}

