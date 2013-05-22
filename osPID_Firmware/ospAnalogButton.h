#ifndef OSPANALOGBUTTON_H
#define OSPANALOGBUTTON_H

#include <Arduino.h>

#include "ospAssert.h"

enum ospAnalogButtonValue
{
	BUTTON_NONE,
	BUTTON_RETURN,	
	BUTTON_UP,
	BUTTON_DOWN,
	BUTTON_OK
};

template<byte analogPin, int buttonValueReturn,
  int buttonValueUp, int buttonValueDown, int buttonValueOk>
class ospAnalogButton {
public:
  ospAnalogButton()
    : activeButton(BUTTON_NONE)
    , currentState(BUTTON_STATE_SCAN)
    , debounceTimer(0)
  {
  }

  ospAnalogButtonValue get()
  {
    ospAnalogButtonValue buttonValue;

    switch (currentState)
    {
    case BUTTON_STATE_SCAN:
      buttonValue = read();

      if (buttonValue != BUTTON_NONE)
      {
        activeButton = buttonValue;

        // start the debounce timer
        debounceTimer = millis();
        debounceTimer += DEBOUNCE_PERIOD;
        currentState = BUTTON_STATE_DEBOUNCE;
      }
      return BUTTON_NONE;
      
    case BUTTON_STATE_DEBOUNCE:
      if (read() == activeButton && millis() >= debounceTimer)
      {
        currentState = BUTTON_STATE_WAIT_RELEASE;
        return activeButton;
      }
      return BUTTON_NONE;
      
    case BUTTON_STATE_WAIT_RELEASE:
      if (read() == BUTTON_NONE)
      {
        // start the debounce timer
        currentState = BUTTON_STATE_DEBOUNCE_RELEASE;
        debounceTimer = millis() + DEBOUNCE_PERIOD;
      }
      return activeButton;
    case BUTTON_STATE_DEBOUNCE_RELEASE:
      if (read() == BUTTON_NONE && millis() >= debounceTimer)
      {
        activeButton = BUTTON_NONE;
        currentState = BUTTON_STATE_SCAN;
      }
      return activeButton;
    }

    // we should never get here, but in case we do:
    ospBugCheck(PSTR("BUTN"), __LINE__);
    return BUTTON_NONE;
  }

private:
  ospAnalogButtonValue read()
  {
    int	buttonValue = analogRead(analogPin);
    
    if (buttonValue >= BUTTON_NONE_THRESHOLD)
      return BUTTON_NONE;
    if (buttonValue <= threshold(buttonValueReturn))
      return BUTTON_RETURN;
    if (buttonValue <= threshold(buttonValueUp))
      return BUTTON_UP;
    if (buttonValue <= threshold(buttonValueDown))
      return BUTTON_DOWN;
    if (buttonValue <= threshold(buttonValueOk))
      return BUTTON_OK;
    
    return BUTTON_NONE;
  }

  enum State {
    BUTTON_STATE_SCAN,
    BUTTON_STATE_DEBOUNCE,
    BUTTON_STATE_WAIT_RELEASE,
    BUTTON_STATE_DEBOUNCE_RELEASE
  };

  enum {
    BUTTON_NONE_THRESHOLD = 1000,
    DEBOUNCE_PERIOD = 10
  };

  inline static int threshold(int expectedValue)
  {
    const double TOLERANCE = 1.1;
    return (int)(expectedValue * TOLERANCE);
  }

private:
  ospAnalogButtonValue activeButton;
  State currentState;
  unsigned long debounceTimer;
};

#endif
