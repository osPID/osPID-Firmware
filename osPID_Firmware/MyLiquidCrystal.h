#ifndef MYLIQUIDCRYSTAL_H
#define MYLIQUIDCRYSTAL_H

#include <LiquidCrystal.h>
#include <avr/progmem.h>

// include a couple of new methods
class MyLiquidCrystal : public LiquidCrystal 
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
    this->print(s);
    this->spc(16 - strlen_P(s));
  }
  
private:

};


#endif



