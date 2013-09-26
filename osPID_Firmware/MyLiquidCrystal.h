#ifndef MYLIQUIDCRYSTAL_H
#define MYLIQUIDCRYSTAL_H

#include <LiquidCrystal.h>
#include <avr/pgmspace.h>

// include a couple of new methods
class MyLiquidCrystal : 
public LiquidCrystal 
{
public:
  MyLiquidCrystal(uint8_t rs, uint8_t enable, uint8_t d0, uint8_t d1, uint8_t d2, uint8_t d3) :  
  LiquidCrystal(rs, enable, d0, d1, d2, d3)
  {
  }

  // print n blanks to LCD
  void spc(byte n)
  {
    for (byte i = n; i > 0; i--)
      this->print(' ');
  }

  // print text from PROGMEM to LCD and fill in with blanks to the end of the line
  void println(const PROGMEM char* s)
  {
    char c;
    byte i = 16;
    while ((c = (char) pgm_read_byte(s++)) && (i > 0))
    {
      this->print(c);
      i--;
    }
    this->spc(i);
  }

private:

};


#endif



