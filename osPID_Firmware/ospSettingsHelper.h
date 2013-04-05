// the settings helper encapsulates CRC handling and space management for the card classes
#ifndef OSPSETTINGSHELPER_H
#define OSPSETTINGSHELPER_H

#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/crc16.h>

class ospSettingsHelper {
private:
  int crc16;
  int address;
  
public:
  ospSettingsHelper(int crcInit, int baseAddress) :
    crc16(crcInit),
    address(baseAddress)
  {
  }

  template<typename T> void save(const T& value) {
    const byte *p = (const byte *)&value;
    
    for (byte n = sizeof(T); n; n--) {
      if (eeprom_read_byte((byte *)address) != *p)
        eeprom_write_byte((byte *)address, *p);
      crc16 = _crc16_update(crc16, *p);
      p++;
      address++;
    }
  }

  template<typename T> void restore(T& value) {
    eepromRead(address, value);
    address += sizeof(T);
  }

  void fillUpTo(int endAddress) {
    while (address < endAddress) {
      if (eeprom_read_byte((byte *)address) != 0xFF) {
        eeprom_write_byte((byte *)address, 0xFF);
      }
      crc16 = _crc16_update(crc16, 0xFF);
      address++;
    }
  }

  void skipTo(int newAddress) {
    address = newAddress;
  }

  int crcValue() const { return crc16; }

  template<typename T> static void eepromRead(unsigned int address, T& value) {
    byte *p = (byte *)&value;

    for (byte n = sizeof(T); n; n--) {
      *p = eeprom_read_byte((byte *)address);
      p++;
      address++;
    }
  }

  template<typename T> static void eepromWrite(unsigned int address, const T& value) {
    const byte *p = (const byte *)&value;

    for (byte n = sizeof(T); n; n--) {
      if (eeprom_read_byte((byte *)address) != *p)
        eeprom_write_byte((byte *)address, *p);
      p++;
      address++;
    }
  }

  // this function uses inline assembler because there is no function in avr-libc
  // which provides the required functionality
  // it is basically a clone of the implementation of eeprom_write_byte, except that
  // we program EEPM1 to 1 to set program-only mode (rather than erase-then-program
  // mode, which is the default of 0)
  template<typename T> static void eepromClearBits(unsigned int address, const T& value) {
    const byte *p = (const byte *)&value;

    for (byte n = sizeof(T); n; n--) {
      __asm__ __volatile__ (
        "1: sbic %0, %1\n" // wait for EEPROM write to complete
        "   rjmp 1b\n"
        "   out %0, __zero_reg__\n" // clear EECR
        "   out %2, %3\n" // set EEPROM address
        "   out %4, %5\n"
        "   out %6, %7\n" // set EEPROM data
        "   sbi %0, %8\n" // set program-only mode
        "   in __tmp_reg__, %9\n" // save flags
        "   cli\n"                // block interrupts
        "   sbi %0, %10\n" // set EEPROM Master Write Enable
        "   sbi %0, %11\n" // trigger write by setting EEPROM Write Enable
        "   out %9, __temp_reg__\n" // restore flags (re-enable interrupts)
        : // no register writes
        : "I" (_SFR_IO_ADDR(EECR)), "I" (EEWE),
          "I" (_SFR_IO_ADDR(EEARH)), "r" (address >> 8),
          "I" (_SFR_IO_ADDR(EEARL)), "r" (address & 0xFF),
          "I" (_SFR_IO_ADDR(EEDR)), "r" (*p),
          "I" (EEPM1),
          "I" (SREG),
          "I" (EEMWE),
          "I" (EEWE)
        : // no clobbers
      );
      p++;
      address++;
    }
  }
};

#endif

