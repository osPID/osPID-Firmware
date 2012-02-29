#ifndef AnalogButton_h
#define AnalogButton_h

#include <inttypes.h>

enum button_t
{
	BUTTON_NONE,
	BUTTON_RETURN,	
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_OK
};

enum buttonState_t
{
	BUTTON_STATE_SCAN,
	BUTTON_STATE_DEBOUNCE,	
	BUTTON_STATE_RELEASE
};

#define	BUTTON_NONE_THRESHOLD 1000	
#define	TOLERANCE 1.1
#define	DEBOUNCE_PERIOD	100

class AnalogButton 
{
	public:
		AnalogButton(uint8_t analogPin, int buttonValueReturn, 
					 int buttonValueUp, int buttonValueDown, 
					 int buttonValueOk);
		
		button_t	get(void);
		
	private:
		button_t	read(void);
		
		// Analog pin used as button multiplexer	
		uint8_t buttonPin; 
		// Upper boound ADC value for each button
		int buttonValueThresholdReturn;
		int buttonValueThresholdUp;
		int buttonValueThresholdDown;
		int buttonValueThresholdOk;
};

#endif
