#include <avr/eeprom.h>
#include <avr/io.h>
#include <util/crc16.h>

#include "ospSettingsHelper.h"

extern void realtimeLoop();

// since EEPROM write time is ~4 ms per byte, we perform an iteration of the realtime loop after
// every byte which requires an actual erase/program cycle
template<size_t size> void ospSettingsHelper::saveSize(const byte *p) 
{
  for (byte n = size; n; n--) 
  {
    if (eeprom_read_byte((byte *)address) != *p) 
    {
      eeprom_write_byte((byte *)address, *p);
      ::realtimeLoop();
    }
    crc16 = _crc16_update(crc16, *p);
    p++;
    address++;
  }
}

template<size_t size> void ospSettingsHelper::eepromReadSize(unsigned int address, byte *p) 
{
  for (byte n = size; n; n--) 
  {
    *p = eeprom_read_byte((byte *)address);
    p++;
    address++;
  }
}

// since EEPROM write time is ~4 ms per byte, we perform an iteration of the realtime loop after
// every byte which requires an actual erase/program
template<size_t size> void ospSettingsHelper::eepromWriteSize(unsigned int address, const byte *p) 
{
  for (byte n = size; n; n--) 
  {
    if (eeprom_read_byte((byte *)address) != *p) 
    {
      eeprom_write_byte((byte *)address, *p);
      ::realtimeLoop();
    }
    p++;
    address++;
  }
}

// this function uses inline assembler because there is no function in avr-libc
// which provides the required functionality
// it is basically a clone of the implementation of eeprom_write_byte, except that
// we program EEPM1 to 1 to set program-only mode (rather than erase-then-program
// mode, which is the default of 0)
//
// NOTE: this function does _not_ perform a realtime loop iteration, since it is called by
// the profile executor, which is _part_ of the realtime loop
template<size_t size> void ospSettingsHelper::eepromClearBitsSize(unsigned int address, const byte *p) 
{
  for (byte n = size; n; n--) 
  {
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
      "   out %9, __tmp_reg__\n" // restore flags (re-enable interrupts)
      : // no register writes
#if defined(EEPE)      
      : "I" (_SFR_IO_ADDR(EECR)), "I" (EEPE),
#else      
      : "I" (_SFR_IO_ADDR(EECR)), "I" (EEWE),
#endif      
        "I" (_SFR_IO_ADDR(EEARH)), "r" (address >> 8),
        "I" (_SFR_IO_ADDR(EEARL)), "r" (address & 0xFF),
        "I" (_SFR_IO_ADDR(EEDR)), "r" (*p),
        "I" (EEPM1),
        "I" (_SFR_IO_ADDR(SREG)),
#if defined(EEMPE)        
        "I" (EEMPE),
#else      
        "I" (EEMWE),
#endif      
#if defined(EEPE)
        "I" (EEPE)
#else      
        "I" (EEWE)
#endif      
      : // no clobbers
    );
    p++;
    address++;
  }
}

template void ospSettingsHelper::saveSize<1u>(const byte *p);
template void ospSettingsHelper::saveSize<2u>(const byte *p);
template void ospSettingsHelper::saveSize<4u>(const byte *p);

template void ospSettingsHelper::eepromReadSize<1u>(unsigned int address, byte *p);
template void ospSettingsHelper::eepromReadSize<2u>(unsigned int address, byte *p);
template void ospSettingsHelper::eepromReadSize<4u>(unsigned int address, byte *p);

template void ospSettingsHelper::eepromWriteSize<1u>(unsigned int address, const byte *p);
template void ospSettingsHelper::eepromWriteSize<2u>(unsigned int address, const byte *p);
template void ospSettingsHelper::eepromWriteSize<4u>(unsigned int address, const byte *p);

template void ospSettingsHelper::eepromClearBitsSize<1u>(unsigned int address, const byte *p);
template void ospSettingsHelper::eepromClearBitsSize<2u>(unsigned int address, const byte *p);
template void ospSettingsHelper::eepromClearBitsSize<4u>(unsigned int address, const byte *p);

