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

  template<typename T> static void eepromRead(int address, T& value) {
    byte *p = (byte *)&value;

    for (byte n = sizeof(T); n; n--) {
      *p = eeprom_read_byte((byte *)address);
      p++;
      address++;
    }
  }

  template<typename T> static void eepromWrite(int address, const T& value) {
    const byte *p = (const byte *)&value;

    for (byte n = sizeof(T); n; n--) {
      if (eeprom_read_byte((byte *)address) != *p)
        eeprom_write_byte((byte *)address, *p);
      p++;
      address++;
    }
  }
};

#endif

