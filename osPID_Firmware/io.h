#define TEMP_INPUT_V100
//#define PROTOTYPE_INPUT
#define DIGITAL_OUTPUT_V100
//#define PROTOTYPE_OUTPUT

union {                // This Data structure lets
  byte asBytes[32];    // us take the byte array
  float asFloat[8];    // sent from processing and
}                      // easily convert it to a
serialXfer;                   // float array
byte b1,b2;



#ifdef TEMP_INPUT_V100
#include "max6675_local.h"
const byte thermistorPin = A6;
const byte thermocoupleCS = 10;
const byte thermocoupleSO = 12;
const byte thermocoupleCLK = 13;
byte inputType=0;
double THERMISTORNOMINAL = 10;
double BCOEFFICIENT = 1;
double TEMPERATURENOMINAL = 293.15;
double REFERENCE_RESISTANCE=10;
MAX6675 thermocouple(thermocoupleCLK, thermocoupleCS, thermocoupleSO);

//eeprom backup / restore

void EEPROMBackupInputParams(int offset)
{
  EEPROM.write(offset, inputType);
  EEPROM_writeAnything(offset+2,THERMISTORNOMINAL);
  EEPROM_writeAnything(offset+6,BCOEFFICIENT);
  EEPROM_writeAnything(offset+10,TEMPERATURENOMINAL);
  EEPROM_writeAnything(offset+14,REFERENCE_RESISTANCE);
}

void EEPROMRestoreInputParams(int offset)
{
  inputType = EEPROM.read(offset);
  EEPROM_readAnything(offset+2,THERMISTORNOMINAL);
  EEPROM_readAnything(offset+6,BCOEFFICIENT);
  EEPROM_readAnything(offset+10,TEMPERATURENOMINAL);
  EEPROM_readAnything(offset+14,REFERENCE_RESISTANCE);
}


void InitializeInputCard()
{


}

void InputSerialReceiveStart()
{

}


void InputSerialReceiveDuring(byte val, byte index)
{
  if(index==1) b1 = val;
  else if(index<18) serialXfer.asBytes[index-2] = val; 
}

void InputSerialReceiveAfter(int eepromOffset)
{
  inputType = b1;
  THERMISTORNOMINAL = serialXfer.asFloat[0];
  BCOEFFICIENT = serialXfer.asFloat[1];
  TEMPERATURENOMINAL = serialXfer.asFloat[2];
  REFERENCE_RESISTANCE = serialXfer.asFloat[3];
  EEPROMBackupInputParams(eepromOffset);
}

void InputSerialSend()
{
  Serial.print((int)inputType); 
  Serial.print(" "); 
  Serial.print(THERMISTORNOMINAL); 
  Serial.print(" ");  
  Serial.print(BCOEFFICIENT); 
  Serial.print(" ");  
  Serial.print(TEMPERATURENOMINAL);   
  Serial.print(" ");  
  Serial.println(REFERENCE_RESISTANCE);   
}

void InputSerialID()
{
  Serial.print(" IID1"); 
}

double readThermistorTemp(int voltage)
{

  float R = REFERENCE_RESISTANCE / (1024.0/(float)voltage - 1);
  float steinhart;
  steinhart = R / THERMISTORNOMINAL;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= BCOEFFICIENT;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (TEMPERATURENOMINAL + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C

  return steinhart;
}

double ReadInputFromCard()
{
  if(inputType == 0) return thermocouple.readCelsius();
  else if(inputType == 1) return readThermistorTemp(analogRead(thermistorPin));
}




#endif /*TEMP_INPUT_V100*/

#ifdef PROTOTYPE_INPUT
 /*Include any  Libraries and/or global variables here*/

float flt1_i=0, flt2_i=0, flt3_i=0, flt4_i=0;
byte bt1_i=0, bt2_i=0, bt3_i=0, bt4_i=0;

void EEPROMBackupInputParams(int offset)
{
  EEPROM_writeAnything(offset, bt1_i);
  EEPROM_writeAnything(offset+1, bt2_i);
  EEPROM_writeAnything(offset+2, bt3_i);
  EEPROM_writeAnything(offset+3, bt4_i);  
  EEPROM_writeAnything(offset+4,flt1_i);
  EEPROM_writeAnything(offset+8,flt2_i);
  EEPROM_writeAnything(offset+12,flt3_i);
  EEPROM_writeAnything(offset+16,flt4_i);
}

void EEPROMRestoreInputParams(int offset)
{
  EEPROM_readAnything(offset, bt1_i);
  EEPROM_readAnything(offset+1, bt2_i);
  EEPROM_readAnything(offset+2, bt3_i);
  EEPROM_readAnything(offset+3, bt4_i);  
  EEPROM_readAnything(offset+4,flt1_i);
  EEPROM_readAnything(offset+8,flt2_i);
  EEPROM_readAnything(offset+12,flt3_i);
  EEPROM_readAnything(offset+16,flt4_i);
}


void InitializeInputCard()
{


}

void InputSerialReceiveStart()
{

}


void InputSerialReceiveDuring(byte val, byte index)
{
  if(index==1) bt1_i = val;
  else if(index==2) bt2_i = val;
  else if(index==3) bt3_i = val;
  else if(index==4) bt4_i = val;
  else if(index<22) serialXfer.asBytes[index-5] = val; 
}

void InputSerialReceiveAfter(int eepromOffset)
{
  flt1_i = serialXfer.asFloat[0];
  flt2_i = serialXfer.asFloat[1];
  flt3_i = serialXfer.asFloat[2];
  flt4_i = serialXfer.asFloat[3];  

  EEPROMBackupInputParams(eepromOffset);
}

void InputSerialSend()
{
  Serial.print(int(bt1_i)); 
  Serial.print(" "); 
  Serial.print(int(bt2_i)); 
  Serial.print(" "); 
  Serial.print(int(bt3_i)); 
  Serial.print(" "); 
  Serial.print(int(bt4_i)); 
  Serial.print(" ");   
  Serial.print(flt1_i); 
  Serial.print(" ");  
  Serial.print(flt2_i); 
  Serial.print(" ");  
  Serial.print(flt3_i);   
  Serial.print(" ");  
  Serial.println(flt4_i);   
}

void InputSerialID()
{
  Serial.print(" IID0"); 
}

double ReadInputFromCard()
{
  /*your code here*/
  return 0;
}
#endif /*PROTOTYPE_INPUT*/



#ifdef DIGITAL_OUTPUT_V100
byte outputType=1;
const byte RelayPin = 5;
const byte SSRPin = 6;
//unsigned long windowStartTime;
double outWindowSec = 5.0;
unsigned long WindowSize = 5000;

void setOutputWindow(double val)
{
  unsigned long temp = (unsigned long)(val*1000);
  if(temp<500)temp = 500;
  outWindowSec = (double)temp/1000;
  if(temp!=WindowSize)
  {
    WindowSize = temp;
  } 
}

void EEPROMBackupOutputParams(int offset)
{
  EEPROM.write(offset, outputType);
  EEPROM_writeAnything(offset+1, WindowSize);
}
void EEPROMRestoreOutputParams(int offset)
{
  outputType = EEPROM.read(offset);
  EEPROM_readAnything(offset+1, WindowSize);
}

void InitializeOutputCard()
{
  pinMode(RelayPin, OUTPUT);
  pinMode(SSRPin, OUTPUT);
}

void OutputSerialReceiveStart()
{

}


void OutputSerialReceiveDuring(byte val, byte index)
{
  if(index==1) b1 = val;
  else if(index<6) serialXfer.asBytes[index-2] = val; 
}

void OutputSerialReceiveAfter(int eepromOffset)
{
  if(outputType != b1)
  {
    if (b1==0)digitalWrite(SSRPin, LOW);
    else if(b1==1) digitalWrite( RelayPin,LOW); //turn off the other pin
    outputType=b1; 
  }
  outWindowSec =  serialXfer.asFloat[0];
  setOutputWindow(outWindowSec);
  EEPROMBackupOutputParams(eepromOffset);

}

void OutputSerialID()
{
  Serial.print(" OID1"); 
}



void WriteToOutputCard(double value)
{
  unsigned long wind = millis() % WindowSize; // (millis() - windowStartTime);
  /*if(wind>WindowSize)
   { 
   wind -= WindowSize;
   windowStartTime += WindowSize;
   }*/
  unsigned long oVal = (unsigned long)(value*(double)WindowSize/ 100.0);
  if(outputType == 0) digitalWrite(RelayPin ,(oVal>wind) ? HIGH : LOW);
  else if(outputType == 1) digitalWrite(SSRPin ,(oVal>wind) ? HIGH : LOW);

}

//serial send / receive
void OutputSerialSend()
{
  Serial.print((int)outputType); 
  Serial.print(" ");  
  Serial.println(outWindowSec); 
}




#endif /*DIGITAL_OUTPUT_V100*/

#ifdef PROTOTYPE_OUTPUT



float flt1_o=0, flt2_o=0, flt3_o=0, flt4_o=0;
byte bt1_o=0, bt2_o=0, bt3_o=0, bt4_o=0;

void EEPROMBackupOutputParams(int offset)
{
  EEPROM_writeAnything(offset, bt1_o);
  EEPROM_writeAnything(offset+1, bt2_o);
  EEPROM_writeAnything(offset+2, bt3_o);
  EEPROM_writeAnything(offset+3, bt4_o);  
  EEPROM_writeAnything(offset+4,flt1_o);
  EEPROM_writeAnything(offset+8,flt2_o);
  EEPROM_writeAnything(offset+12,flt3_o);
  EEPROM_writeAnything(offset+16,flt4_o);
}

void EEPROMRestoreOutputParams(int offset)
{
  EEPROM_readAnything(offset, bt1_o);
  EEPROM_readAnything(offset+1, bt2_o);
  EEPROM_readAnything(offset+2, bt3_o);
  EEPROM_readAnything(offset+3, bt4_o);  
  EEPROM_readAnything(offset+4,flt1_o);
  EEPROM_readAnything(offset+8,flt2_o);
  EEPROM_readAnything(offset+12,flt3_o);
  EEPROM_readAnything(offset+16,flt4_o);
}

void InitializeOutputCard()
{
}

void OutputSerialReceiveStart()
{

}


void OutputSerialReceiveDuring(byte val, byte index)
{
  if(index==1) bt1_o = val;
  else if(index==2) bt2_o = val;
  else if(index==3) bt3_o = val;
  else if(index==4) bt4_o = val;
  else if(index<22) serialXfer.asBytes[index-5] = val; 
}

void OutputSerialReceiveAfter(int eepromOffset)
{
  flt1_o = serialXfer.asFloat[0];
  flt2_o = serialXfer.asFloat[1];
  flt3_o = serialXfer.asFloat[2];
  flt4_o = serialXfer.asFloat[3];  

  EEPROMBackupOutputParams(eepromOffset);
}

void OutputSerialID()
{
  Serial.print(" OID0"); 
}



void WriteToOutputCard(double value)
{


}

//serial send / receive
void OutputSerialSend()
{
  Serial.print(int(bt1_o)); 
  Serial.print(" "); 
  Serial.print(int(bt2_o)); 
  Serial.print(" "); 
  Serial.print(int(bt3_o)); 
  Serial.print(" "); 
  Serial.print(int(bt4_o)); 
  Serial.print(" ");   
  Serial.print(flt1_o); 
  Serial.print(" ");  
  Serial.print(flt2_o); 
  Serial.print(" ");  
  Serial.print(flt3_o);   
  Serial.print(" ");  
  Serial.println(flt4_o);  
}

#endif /*PROTOTYPE_OUTPUT*/

