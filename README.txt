/********************************************************
 * osPID Firmware,  Version 1.6
 * by Brett Beauregard & Rocket Scream
 * License: GPLv3 & BSD License (For autotune)
 * August 2012
 ********************************************************/

 NOTE:  THIS FIRMWARE IS CONFIGURED FOR DIGITAL OUTPUT CARD 
		V1.5 & TEMPERATURE INPUT CARD V1.2.  IF YOU ARE USING 
		A DIFFERENT I/O CONFIGURATION BE SURE TO UN-COMMENT THE 
		APPROPRIATE	#DEFINE STATEMENTS IN IO.H.
 
Updates for version 1.6
-added support for v1.5 of the Temperature Input card (MAX31855 Thermocouple chip)
 
Updates for version 1.5
-restructured code to allow for different IO cards
-added reflow profile support
-eliminated LCD flicker
-error message when thermocouple is disconnected
-extreme code size / RAM improvement (mainly menu and EEPRom)
-consolodated the code into fewer files
 * osPID_Firmware.ino - Just about everything
 * io.h - IO card code.  pre-compiler flags control which card code is used
 * EEPROMAnything.h - halley's amazing EEPROMWriteAnything code.
 * AnalogButton .cpp _local.h - ospid button-reading/debounce code
 * PID_AutoTune_v0 .cpp _local.h - local copy of the autotune library (to avoid
   conflicts with possibly pre-installed copies)
 * PID_v1 .ccp _local.h - local copy of the PID library
 * max6675 .cpp _local.h - local copy of the max6675 library, used by the input card.