// the settings helper encapsulates CRC handling and space management for the card classes
#ifndef OSPSETTINGSHELPER_H
#define OSPSETTINGSHELPER_H

#include <Arduino.h>
#include <avr/eeprom.h>
#include <util/crc16.h>

class ospSettingsHelper {
private:
  unsigned int crc16;
  int address;

  // using private size-templated methods lets there be only 3 instantiations
  // for every method, regardless of how many types are used in the type-templated
  // public methods
  template<size_t size> void saveSize(const byte *p);
  template<size_t size> static void eepromReadSize(unsigned int address, byte *p);
  template<size_t size> static void eepromWriteSize(unsigned int address, const byte *p);
  template<size_t size> static void eepromClearBitsSize(unsigned int address, const byte *p);
  
public:
  ospSettingsHelper(unsigned int crcInit, int baseAddress) :
    crc16(crcInit),
    address(baseAddress)
  {
  }

  template<typename T> void save(const T& value) {
    const byte *p = (const byte *)&value;
    
    saveSize<sizeof(T)>(p);
  }

  template<typename T> void restore(T& value) {
    eepromRead(address, value);
    address += sizeof(T);
  }

  void fillUpTo(int endAddress) {
    byte ff = 0xFF;
    while (address < endAddress) {
      save(ff);
    }
  }

  void skipTo(int newAddress) {
    address = newAddress;
  }

  unsigned int crcValue() const { return crc16; }

  template<typename T> static void eepromRead(unsigned int address, T& value) {
    byte *p = (byte *)&value;

    eepromReadSize<sizeof(T)>(address, p);
  }

  template<typename T> static void eepromWrite(unsigned int address, const T& value) {
    const byte *p = (const byte *)&value;

    eepromWriteSize<sizeof(T)>(address, p);
  }

  // this function uses inline assembler because there is no function in avr-libc
  // which provides the required functionality
  // it is basically a clone of the implementation of eeprom_write_byte, except that
  // we program EEPM1 to 1 to set program-only mode (rather than erase-then-program
  // mode, which is the default of 0)
  template<typename T> static void eepromClearBits(unsigned int address, const T& value) {
    const byte *p = (const byte *)&value;

    eepromClearBitsSize<sizeof(T)>(address, p);
  }

};

#endif

