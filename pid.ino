void changeMode(byte newMode)
{
  valItems[2].editable = (newMode==MANUAL);
  myPID.SetMode(newMode); 
}



void changeAutoTune(byte dummy)
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
  navItems[2].stringIndex = start ? 3:2;
  if(start)
  {
    ATuneModeRemember = myPID.GetMode();
    myPID.SetMode(MANUAL);
    //lock out UI mode and output changes
    optItems[0].curOption = 0;
    optItems[0].editable = false; //mode
    valItems[2].editable = false; //output
  }
  else
  {
    myPID.SetMode(ATuneModeRemember);
    optItems[0].curOption = ATuneModeRemember;
    optItems[0].editable = true; //mode
    if(ATuneModeRemember==MANUAL) valItems[2].editable = true;
  } 
}

