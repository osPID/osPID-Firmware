#include "AnalogButton_local.h"
//#include "WProgram.h"
#include "Arduino.h"

AnalogButton::AnalogButton(uint8_t analogPin, int buttonValueReturn, 
							int buttonValueUp, int buttonValueDown, int buttonValueOk)
{
	// Store analog pin used to multiplex push button
	buttonPin = analogPin;
	
	// Add upper bound of tolerance for variation againts resistor values, temperature
	// and other possible drift
	buttonValueThresholdReturn = TOLERANCE*buttonValueReturn;
	buttonValueThresholdUp = TOLERANCE*buttonValueUp;
	buttonValueThresholdDown = TOLERANCE*buttonValueDown;
	buttonValueThresholdOk = TOLERANCE*buttonValueOk;
}

button_t	AnalogButton::read(void)
{
	int	buttonValue;
	
	buttonValue = analogRead(buttonPin);

	if (buttonValue >= BUTTON_NONE_THRESHOLD)		return BUTTON_NONE;
	if (buttonValue <= buttonValueThresholdReturn)	return BUTTON_RETURN;
	if (buttonValue <= buttonValueThresholdUp)		return BUTTON_UP;
	if (buttonValue <= buttonValueThresholdDown)	return BUTTON_DOWN;
	if (buttonValue <= buttonValueThresholdOk)		return BUTTON_OK;
	
	return BUTTON_NONE;
}

button_t	AnalogButton::get(void)
{
	static	button_t	buttonMask;
	static	buttonState_t	buttonState;
	static	unsigned long debounceTimer;
	button_t	buttonValue;
	button_t	buttonStatus;
	
	// Initialize button status
	buttonStatus = BUTTON_NONE;
	
	switch (buttonState)
	{
		case BUTTON_STATE_SCAN:
			// Retrieve current button value
			buttonValue = read();
			// If button press is detected
			if (buttonValue != BUTTON_NONE)
			{
				// Store current button press value
				buttonMask = buttonValue;
				// Retrieve current time
				debounceTimer = millis();
				debounceTimer += DEBOUNCE_PERIOD;
				// Proceed to button debounce state
				buttonState = BUTTON_STATE_DEBOUNCE;
			}
			break;
			
		case BUTTON_STATE_DEBOUNCE:
			if (read() == buttonMask)
			{
				// If debounce period is completed
				if (millis() >= debounceTimer)
				{
					buttonStatus = buttonMask;
					// Proceed to wait for the button to be released
					buttonState = BUTTON_STATE_RELEASE;
				}
			}
			break;
			
		case BUTTON_STATE_RELEASE:
			if (read() == BUTTON_NONE)
			{
				buttonMask = BUTTON_NONE;
				buttonState = BUTTON_STATE_SCAN;
			}
			break;
	}
	
	return (buttonStatus);
}
