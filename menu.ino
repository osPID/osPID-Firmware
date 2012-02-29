/*********************************************************
 * Declarations
 *********************************************************/
const byte VALUE = 0;
const byte NAV = 1;
const byte OPTION = 2;

prog_char string_0[] PROGMEM = "DashBrd";
prog_char string_1[] PROGMEM = "Config ";
prog_char string_2[] PROGMEM = "ATune  ";
prog_char string_3[] PROGMEM = "Cancel ";
prog_char string_4[] PROGMEM = " Auto  ";
prog_char string_5[] PROGMEM = " Man   ";


PROGMEM const char *string_table[] =
{
  string_0,
  string_1,
  string_2,
  string_3,
  string_4,
  string_5,
};

char buffer[7];

struct ItemChangeEvent {
  const byte &val;
};
typedef void (*cb_change)(byte);

struct menuItem
{
  byte type;
  byte index;
};

struct navItem
{
  byte stringIndex;  
  cb_change navCommand;
  byte navParam;
};

struct valItem
{
  boolean editable;
  char icon;
  double *value;
  double Min;
  double Max;
  byte nbefore;
  double lastval;
};

struct optItem
{
  boolean editable;
  char icon; 
  byte curOption;
  byte optCount;
  cb_change optChanged;
  byte options[3];
};



boolean showBlink;

/*********************************************************
 * Menu Creation
 ********************************************************/
valItem valItems[] = {
  (valItem){ true,'S',&setpoint,-999.9,999.9,4,-1000  }   ,
  (valItem){ false,'I',&input,-999.9,999.9,4,-1000  }   ,
  (valItem){ true,'O',&output,-999.9,999.9,4,-1000  }   ,

  (valItem){ true,'P',&kp,0,99.99,3,-1  }  ,
  (valItem){ true,'I',&ki,0,99.99,3,-1  }  ,
  (valItem){ true,'D',&kd,0,99.99,3,-1  }  ,
  (valItem){ true,'W',&outWindowSec,0,999.9,4,-1}
};
  
optItem optItems[] = {
  (optItem){ true,'M',1,2,changeMode,{5,4,0}}
};

navItem navItems[] = {
  (navItem){0,MnuChange,1  },       //to dashboard
  (navItem){1,MnuChange,2  },       //to config
  (navItem){2,changeAutoTune,0  }   //autotune
};

menuItem items[] = 
{
  //Grp0
  (menuItem){ NAV,0  }, //dashbrd
  (menuItem){ NAV,1  }, //config
  (menuItem){ NAV,2  }, //atune
  //Grp1
  (menuItem){ VALUE,0  }, //setpoint
  (menuItem){ VALUE,1  }, //input
  (menuItem){ VALUE,2  }, //output
  (menuItem){ OPTION,0  }, //mode
  //Grp2
  (menuItem){ VALUE, 3  }, //Kp
  (menuItem){ VALUE, 4  }, //Ki
  (menuItem){ VALUE, 5  }, //Kd
  (menuItem){ VALUE, 6  }, //outwndw  
};

const byte grp0[] = {
  0,1,2}; 
const byte grp1[] = {
  3,4,5,6};
const byte grp2[] = {
  7,8,9,10};

const byte *menu[] = {
  grp0,grp1,grp2};
const byte groupCount[] = {
  3,4,4};
const byte menuBack[] ={
  0,0,0};

/*********************************************************
 * Menu Functions (Drawing, Navigation)
 *********************************************************/
void DrawLCD()
{
  byte start,iend;
  if(groupCount[menuIndex]<2)
  {
    start = 0;// itemIndex; 
    iend = 1;
  }
  else
  {
    start = itemIndex>lastIndex? lastIndex: itemIndex;
    iend = 2;
    if(start==groupCount[menuIndex]-1)start = groupCount[menuIndex]-2; //does this ever get used?
  }

  lcd.clear();
  for(byte i=0;i<iend;i++)
  {
    lcd.setCursor(0,i);
    PrintItemToLCD(start+i, (start+i==itemIndex));
  }
  if(tuning)
  {
    unsigned long now = millis();
    if ((now - blinkTime)>1000)
    {
      blinkTime = now;
      showBlink = !showBlink;
    }
    lcd.setCursor(7,showBlink?1:0);
    lcd.print("*");

  }
  if(cursorX >0)
  {
    lcd.cursor();
    lcd.setCursor(cursorX,itemIndex-start);
  }
  else lcd.noCursor();

}


void PrintItemToLCD(byte ind, boolean highlighted)
{
  menuItem m = items[menu[menuIndex][ind]]; 
  if(m.type==NAV)
  {
    navItem n = navItems[m.index]; 
    strcpy_P(buffer, (char*)pgm_read_word(&string_table[n.stringIndex])); 
    lcd.print(highlighted ? '>':' ');
    lcd.print(buffer);     
  }
  else if(m.type==OPTION)
  {
    optItem o = optItems[m.index]; 
    strcpy_P(buffer, (char*)pgm_read_word(&string_table[o.options[o.curOption]])); 
    char c = ' ';
    if(highlighted)
    {
      if(!o.editable) c='|';
      else if(cursorX>0) c='[';
      else c = '>';
    }
    lcd.print(c);
    lcd.print(o.icon);
    lcd.print(buffer); //buffer here is 1 longer than the LCD space.  problem?
  }
  else if(m.type==VALUE)
  {
    valItem v = valItems[m.index];   
    double tmp = *v.value;
    byte  nafter= (v.nbefore < 5) ? 5-v.nbefore : 0;
    int whole = (int)tmp;
    String s = String(whole);
    byte pad = v.nbefore-s.length();		
    for(byte i=0;i<pad;i++)s = ' '+s;
    if(nafter>0)
    { 
      s+= '.';
      double rem = tmp-whole;
      if(rem<0)rem = 0 - rem;
      for(byte i=0;i<nafter;i++)
      {
        rem*=10;
        s+=String((int)rem);
        rem-=(int)rem;
      }
    }
    char c = ' ';
    if(highlighted)
    {
      if(!v.editable) c='|';
      else if(cursorX>0) c='[';
      else c = '>';
    }
    lcd.print(c);
    lcd.print(v.icon);
    lcd.print(s);
  }
}

void up()
{
  if (cursorX==0)
  {
    if(itemIndex>0)
    {
    lastIndex=itemIndex;
    itemIndex--;
    }
    //if(itemIndex<0)itemIndex=0;
  }
  else
  {
    menuItem m = items[menu[menuIndex][itemIndex]]; 
    if(m.type==VALUE)
    {
      valItem v = valItems[m.index];
      double extra=1;
      if(cursorX>2+v.nbefore)
      {
        for(byte i=3+v.nbefore;i<=cursorX;i++)
        {
          extra/=10;
        }
      }
      else
      {
        for(byte i=cursorX; i<1+v.nbefore;i++)
        {
          extra*=10;
        }
      }
      *v.value+=extra;
      Serial.println(*v.value);
      if(*v.value>v.Max)*v.value=v.Max;
    }
    else if(m.type==OPTION)
    {
      byte c = optItems[m.index].curOption;
      if(c<=0)c = optItems[m.index].optCount-1;
      else c--;
      optItems[m.index].curOption = c;
      if(optItems[m.index].optChanged)(*optItems[m.index].optChanged)(c);

    }
  }
  DrawLCD(); 
}
void down()
{
  if (cursorX==0)
  {
    if(itemIndex<groupCount[menuIndex]-1)
    {
      lastIndex=itemIndex;
      itemIndex++;
    }
    //if(itemIndex>groupCount[menuIndex]-1) itemIndex = groupCount[menuIndex]-1;
  }
  else
  {
    menuItem m = items[menu[menuIndex][itemIndex]]; 
    if(m.type==VALUE)
    {
      valItem v = valItems[m.index];
      double extra=1;
      if(cursorX>2+v.nbefore)
      {
        for(byte i=3+v.nbefore;i<=cursorX;i++)
        {
          extra/=10;
        }
      }
      else
      {
        for(byte i=cursorX; i<1+v.nbefore;i++)
        {
          extra*=10;
        }
      }
      *v.value-=extra;
      if(*v.value<v.Min)*v.value=v.Min;
    }
    else if(m.type==OPTION)
    {
      byte c = optItems[m.index].curOption;
      if(c>=optItems[m.index].optCount-1)c = 0;
      else c++;     
      optItems[m.index].curOption = c;
      if(optItems[m.index].optChanged)(*optItems[m.index].optChanged)(c);
    }
  } 
  DrawLCD(); 
}
void ok()
{
  menuItem m = items[menu[menuIndex][itemIndex]];
  if(m.type==NAV)
  {
    navItem n = navItems[m.index];
    if(n.navCommand)(*n.navCommand)(n.navParam);
  }
  else if(m.type==OPTION)
  {
    if(cursorX==0)
    {
      optItem o = optItems[m.index];
      if(o.editable)
      {      
          cursorX=3;
      }
    }
  }
  else if(m.type==VALUE)
  {
    valItem v = valItems[m.index];
    if(v.editable)
    {
      if(cursorX==0)cursorX=3;
      else if (cursorX<7)cursorX++;
      if(cursorX == 2 + v.nbefore) cursorX++;
    }
  }
  DrawLCD(); 
}

void back()
{
  if(cursorX==0)
  { //we weren't editing, go back a menu

    //depending on which menu we're coming back from, we may need to write to the eeprom
    if(menuIndex==1)
    { 
        EEPROMBackupDash();
    }
    else if(menuIndex==2) //tunings may have changed
    {
      if( eepromTunings.asFloats[0]!=kp ||
        eepromTunings.asFloats[1]!=ki ||
        eepromTunings.asFloats[2]!=kd){
        eepromTunings.asFloats[0]=kp;
        eepromTunings.asFloats[1]=ki;
        eepromTunings.asFloats[2]=kd;
        EEPROMBackupTunings();
        myPID.SetTunings(kp,ki,kd);
      }
      setOutputWindow(outWindowSec);
    }

    menuIndex = menuBack[menuIndex];
    itemIndex=0; 
    lastIndex=1;
  }
  else if(cursorX<=3)
  { //exit editing
    cursorX=0;
  }
  else
  { //this should only happen for VALUE types, but just make sure
    menuItem m = items[menu[menuIndex][itemIndex]];
    if(m.type==VALUE)
    {
      valItem v = valItems[m.index];
      cursorX--;
      if(cursorX == 2 + v.nbefore) cursorX--;
    }
  }
  DrawLCD();
}

void MnuChange(byte Id)
{
  menuIndex=Id;
  itemIndex=0; 
  lastIndex=1; 
}

void setOutputWindow(double val)
{
   unsigned long temp = (unsigned long)(val*1000);
   if(temp<500)temp = 500;
   outWindowSec = (double)temp/1000;
   if(temp!=WindowSize)
   {
      WindowSize = temp;
      EEPROMBackupOutput(); 
   } 
}

