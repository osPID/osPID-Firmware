void initializeEEPROM()
{
  //read in eeprom values
  byte firstTime = EEPROM.read(0);
  if(firstTime!=1)
  {//the only time this won't be 1 is the first time the program is run after a reset.
    //initialize with default values
    eepromTunings.asFloats[0]=kp;
    eepromTunings.asFloats[1]=ki;
    eepromTunings.asFloats[2]=kd;
    eepromWindowSize.asLong = WindowSize;

    eepromATune.asFloats[0]=aTuneStep;
    eepromATune.asFloats[1]=aTuneNoise;
    eepromATune.asFloats[2]=aTuneLookBack;

    eepromThermistor.asFloats[0] = THERMISTORNOMINAL;
    eepromThermistor.asFloats[1] = BCOEFFICIENT;
    eepromThermistor.asFloats[2] = TEMPERATURENOMINAL;
    kp = 3;
    //put them into the eeprom for next time
    EEPROM.write(eepromTuningOffset, 0); //default action is DIRECT
    for(int i=0;i<12;i++)
    {
      EEPROM.write(eepromTuningOffset+i+1,eepromTunings.asBytes[i]);
      EEPROM.write(eepromATuneOffset+i,eepromATune.asBytes[i]);
      EEPROM.write(eepromThermistorOffset+i,eepromThermistor.asBytes[i]);
    }
    for(int i=0;i<4;i++)EEPROM.write(eepromWindowSizeOffset+i,eepromWindowSize.asBytes[i]);
    EEPROM.write(eepromIOTypeOffset,inputType);
    EEPROM.write(eepromIOTypeOffset+1,outputType);

    EEPROMBackupDash();
    EEPROMBackupOutput();
    
    EEPROM.write(0,1); //so that first time will never be true again
  }
  else
  {
    myPID.SetControllerDirection((int)EEPROM.read(eepromTuningOffset));
    for(int i=0;i<12;i++)
    {
      eepromTunings.asBytes[i] = EEPROM.read(eepromTuningOffset+i+1);
      eepromATune.asBytes[i] = EEPROM.read(eepromATuneOffset+i);
      eepromThermistor.asBytes[i] = EEPROM.read(eepromThermistorOffset+i);
    }
    for(int i=0;i<4;i++)eepromWindowSize.asBytes[i] = EEPROM.read(eepromWindowSizeOffset+i);
    inputType = EEPROM.read(eepromIOTypeOffset);
    outputType = EEPROM.read(eepromIOTypeOffset+1);
    kp = eepromTunings.asFloats[0];
    ki = eepromTunings.asFloats[1];
    kd = eepromTunings.asFloats[2];

    WindowSize = eepromWindowSize.asLong;
    outWindowSec = (double)WindowSize/1000;
    aTuneStep = eepromATune.asFloats[0];
    aTuneNoise = eepromATune.asFloats[1];
    aTuneLookBack = eepromATune.asFloats[2];
    THERMISTORNOMINAL = eepromThermistor.asFloats[0];
    BCOEFFICIENT = eepromThermistor.asFloats[1];
    TEMPERATURENOMINAL = eepromThermistor.asFloats[2];
    
    changeMode((int)EEPROM.read(eepromDashOffset));
    for(int i=0;i<8;i++)
    {
       eepromDash.asBytes[i] = EEPROM.read(eepromDashOffset+i+1); 
    }
    setpoint = eepromDash.asFloats[0];
    output = eepromDash.asFloats[1];
  }
}

void EEPROMreset()
{
 EEPROM.write(0,0); 
}

void EEPROMBackupTunings()
{
    EEPROM.write(eepromTuningOffset,ctrlDirection);
    eepromTunings.asFloats[0]=kp;
    eepromTunings.asFloats[1]=ki;
    eepromTunings.asFloats[2]=kd;
    for(byte i=0;i<12;i++)
    {
      EEPROM.write(eepromTuningOffset+i+1,eepromTunings.asBytes[i]);
    }
}

void EEPROMBackupIOParams()
{
  EEPROM.write(eepromIOTypeOffset, inputType);
  EEPROM.write(eepromIOTypeOffset+1, outputType);
  eepromThermistor.asFloats[0] = THERMISTORNOMINAL;
  eepromThermistor.asFloats[1] = BCOEFFICIENT;
  eepromThermistor.asFloats[2] = TEMPERATURENOMINAL;
  
  for(byte i=0;i<12;i++)
  {
    EEPROM.write(eepromThermistorOffset+i,eepromThermistor.asBytes[i]);
  }
}

void EEPROMBackupDash()
{
    EEPROM.write(eepromDashOffset, (byte)myPID.GetMode());
   eepromDash.asFloats[0] = setpoint;
   eepromDash.asFloats[1] = output;
   for(byte i=0;i<8;i++)
  {
    EEPROM.write(eepromDashOffset+i+1,eepromDash.asBytes[i]);
  }
}

void EEPROMBackupOutput()
{
   eepromWindowSize.asLong = WindowSize; 
   for(int i=0;i<4;i++)EEPROM.write(eepromWindowSizeOffset+i,eepromWindowSize.asBytes[i]); 
}
