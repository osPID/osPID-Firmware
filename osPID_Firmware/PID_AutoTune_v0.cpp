/**********************************************************************************************
 * Arduino PID AutoTune Library - Version 0.0.0
 * by Brett Beauregard <br3ttb@gmail.com> brettbeauregard.com
 *
 * This Library is ported from the AutotunerPID Toolkit by William Spinelli
 * (http://www.mathworks.com/matlabcentral/fileexchange/4652) 
 * Copyright (c) 2004
 *
 * This Library is licensed under the BSD License:
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright 
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright 
 *       notice, this list of conditions and the following disclaimer in 
 *       the documentation and/or other materials provided with the distribution
 *       
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 * POSSIBILITY OF SUCH DAMAGE.
 **********************************************************************************************/

#if ARDUINO >= 100
  #include "Arduino.h"
#else
  #include "WProgram.h"
#endif

#include "PID_AutoTune_v0_local.h" //renamed to avoid conflict if Autotune library is installed on IDE


PID_ATune::PID_ATune(double* Input, double* Output)
{
	input = Input;
	output = Output;
	controlType =0 ; //default to PI
	noiseBand = 0.5;
	running = false;
	oStep = 30;
	SetLookbackSec(10);
	lastTime = millis();
	
}



void PID_ATune::Cancel()
{
	running = false;
} 
 
int PID_ATune::Runtime()
{
	justevaled=false;
	if(peakCount>9 && running)
	{
		running = false;
		FinishUp();
		return 1;
	}
	unsigned long now = millis();
	
	if((now-lastTime)<(unsigned long)sampleTime) return false;
	lastTime = now;
	double refVal = *input;
	justevaled=true;
	if(!running)
	{ //initialize working variables the first time around
		peakType = 0;
		peakCount=0;
		justchanged=false;
		absMax=refVal;
		absMin=refVal;
		setpoint = refVal;
		running = true;
		initCount=0;
		outputStart = *output;
		*output = outputStart+oStep;
	}
	else
	{
		if(refVal>absMax)absMax=refVal;
		if(refVal<absMin)absMin=refVal;
	}
	
	//oscillate the output base on the input's relation to the setpoint
	
	if(refVal>setpoint+noiseBand) *output = outputStart-oStep;
	else if (refVal<setpoint-noiseBand) *output = outputStart+oStep;
	
	
  //bool isMax=true, isMin=true;
  isMax=true;isMin=true;
  //id peaks
  for(int i=nLookBack-1;i>=0;i--)
  {
    double val = lastInputs[i];
    if(isMax) isMax = refVal>val;
    if(isMin) isMin = refVal<val;
    lastInputs[i+1] = lastInputs[i];
  }
  lastInputs[0] = refVal;  
  if(nLookBack<9)
  {  //we don't want to trust the maxes or mins until the inputs array has been filled
	initCount++;
	return 0;
	}
  
  if(isMax)
  {
    if(peakType==0)peakType=1;
    if(peakType==-1)
    {
      peakType = 1;
      justchanged=true;
      peak2 = peak1;
    }
    peak1 = now;
    peaks[peakCount] = refVal;
   
  }
  else if(isMin)
  {
    if(peakType==0)peakType=-1;
    if(peakType==1)
    {
      peakType=-1;
      peakCount++;
      justchanged=true;
    }
    
    if(peakCount<10)peaks[peakCount] = refVal;
  }
  
  if(justchanged && peakCount>2)
  { //we've transitioned.  check if we can autotune based on the last peaks
    double avgSeparation = (abs(peaks[peakCount-1]-peaks[peakCount-2])+abs(peaks[peakCount-2]-peaks[peakCount-3]))/2;
    if( avgSeparation < 0.05*(absMax-absMin))
    {
		FinishUp();
      running = false;
	  return 1;
	 
    }
  }
   justchanged=false;
	return 0;
}
void PID_ATune::FinishUp()
{
	  *output = outputStart;
      //we can generate tuning parameters!
      Ku = 4*oStep/((absMax-absMin)*3.14159);
      Pu = (double)(peak1-peak2) * 0.001;
}

double PID_ATune::GetKp()
{
	return controlType==1 ? 0.6 * Ku : 0.4 * Ku;
}

double PID_ATune::GetKi()
{
	return controlType==1? 1.2*Ku / Pu : 0.48 * Ku / Pu;  // Ki = Kc/Ti
}

double PID_ATune::GetKd()
{
	return controlType==1? 0.075 * Ku * Pu : 0;  //Kd = Kc * Td
}

void PID_ATune::SetOutputStep(double Step)
{
	oStep = Step;
}

double PID_ATune::GetOutputStep()
{
	return oStep;
}

void PID_ATune::SetControlType(int Type) //0=PI, 1=PID
{
	controlType = Type;
}
int PID_ATune::GetControlType()
{
	return controlType;
}
	
void PID_ATune::SetNoiseBand(double Band)
{
	noiseBand = Band;
}

double PID_ATune::GetNoiseBand()
{
	return noiseBand;
}

void PID_ATune::SetLookbackSec(int value)
{
    if (value<1) value = 1;
	
	if(value<25)
	{
		nLookBack = value * 4;
		sampleTime = 250;
	}
	else
	{
		nLookBack = 100;
		sampleTime = value*10;
	}
}

int PID_ATune::GetLookbackSec()
{
	return nLookBack * sampleTime / 1000;
}

