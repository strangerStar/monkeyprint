
#include <avr/io.h>
#include <avr/wdt.h>	// Watchdog timer configuration functions.
#include <avr/power.h>	// Clock prescaler configuration functions.
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <stdio.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include "../hardware.h"
#include "printerFunctions.h"
#include "menu.h"


// *****************************************************************************
// Basic variables. ************************************************************
// *****************************************************************************
uint8_t printerOperatingFlag = 0;
uint8_t printerReadyFlag = 1;
uint8_t printerState = 0;	// 0: idle, 1: printing
uint16_t slice = 1;
uint16_t numberOfSlices = 1;


// *****************************************************************************
// Tilt variables and functions.************************************************
// *****************************************************************************

// Tilt. ***********************************************************************
uint8_t tiltSpeed;
#define TILT_SPEED_MIN 1
#define TILT_SPEED_MAX 10
uint8_t tiltSpeedEep EEMEM;

uint8_t tiltAngle = 14;
#define TILT_ANGLE_MIN 9
#define TILT_ANGLE_MAX 18
uint8_t tiltAngleEep EEMEM;

uint16_t tiltCompareValue;
volatile uint16_t tiltCounter = 0;
//volatile uint16_t tiltCounterMax = 0;
volatile uint8_t tiltingFlag = 0;
uint16_t tiltAngleSteps;


// Return stepper status (idle: 0, running: 1). ********************************
uint8_t tiltStepperRunning( void )
{
	return (TCCR3B & (1 << CS30));
}
// Enable stepper. *************************************************************
void enableTiltStepper( void )
{
	TILTENABLEPORT |= (1 << TILTENABLEPIN);
	//TCCR3B |= (1 << CS31);
	TCCR3B |= (1 << CS30);
}
// Disable stepper. ************************************************************
void disableTiltStepper ( void )
{
	// Stop.
	TILTENABLEPORT &= ~(1 << TILTENABLEPIN);
	//TCCR3B &= ~(1 << CS31);
	TCCR3B &= ~(1 << CS30);
}
// Set forward direction. ******************************************************
void tiltStepperSetForward ( void )
{
	TILTDIRPORT &= ~(1 << TILTDIRPIN);
}
// Set backward direction. *****************************************************
void tiltStepperSetBackward ( void )
{
	TILTDIRPORT |= (1 << TILTDIRPIN);
}
// Check stepper direction (0: backward, 1: forward). **************************
uint8_t tiltStepperGetDirection(void)
{
	return !(TILTDIRPORT & (1 << TILTDIRPIN));
}
// Initialise turn with given angle and speed. *********************************
void tilt(uint8_t inputAngle, uint8_t inputSpeed)
{
	// Tilt angle 10--180° in steps of 10° --> 1--18.
	// Tilt speed 0.25--2.5 Hz in steps of 0.25 Hz --> 1--10. See log file for calculations.
	// Timer compare value = -158 * x + 1738 with x ranging from 1--10. Output will be 
	int16_t tiltTimerCompareValue = inputSpeed * -158;
	tiltTimerCompareValue += 1738;
	tiltTimerCompareValue /= 10;
// TODO ordentlich machen
//	tiltTimerCompareValue *= 2;
	timer3SetCompareValue(tiltTimerCompareValue);

	// Tilt angle 1--18 (10°--180°) --> 89--1600 steps (3200 steps per rev).
	tiltAngleSteps = inputAngle * 89;

	// Reset tilt counter.
	tiltCounter = 0;
	
	// Set direction.
	tiltStepperSetForward();
	
	// Enable tilt stepper.
	enableTiltStepper();	
	
	ledYellowOn();
}

// Control tilt. ***************************************************************
void tiltControl (void)
{
	// Check direction. Only reverse if forward.
//	if (!(TILTDIRPOLL & (1 << TILTDIRPIN)))
	if (tiltStepperGetDirection())
	{
		// Increase step count and compare to target count.
		if (++tiltCounter == tiltAngleSteps)
		{
			ledGreenOn();
			ledYellowOff();
			// Run stepper backwards.
			tiltStepperSetBackward();
			// TILTDIRPORT ^= (1 << TILTDIRPIN);
		}
	}
}

// Adjust tilt angle. **********************************************************
void tiltAdjustAngle (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if (++tiltAngle > TILT_ANGLE_MAX) tiltAngle = TILT_ANGLE_MAX;
	}
	// Decrease if input = 2.
	else if (input==2)
	{
		if (--tiltAngle < TILT_ANGLE_MIN) tiltAngle = TILT_ANGLE_MIN;
	}
	menuValueSet(tiltAngle,13);
	eeprom_update_byte (&tiltAngleEep, tiltAngle);
}

// Adjust tilt speed. **********************************************************
void tiltAdjustSpeed (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if (++tiltSpeed > TILT_SPEED_MAX) tiltSpeed = TILT_SPEED_MAX;
	}
	// Decrease if input = 2.
	else if (input==2)
	{
		if (--tiltSpeed < TILT_SPEED_MIN) tiltSpeed = TILT_SPEED_MIN;
	}
	menuValueSet(tiltSpeed,14);
	eeprom_update_byte (&tiltSpeedEep, tiltSpeed);
}


// Set tilt angle. *************************************************************
void tiltSetAngle (uint8_t input)
{
	if (input>=TILT_ANGLE_MAX)
	{
		tiltAngle = TILT_ANGLE_MAX;
	}
	else if (input<TILT_ANGLE_MIN)
	{
		tiltAngle = TILT_ANGLE_MIN;
	}
	else
	{
		tiltAngle = input;
	}
	
	menuValueSet(tiltAngle,13);
	eeprom_update_byte (&tiltAngleEep, tiltAngle);
}



// Set tilt speed. *************************************************************
void tiltSetSpeed (uint8_t input)
{
	if (input>=TILT_SPEED_MAX)
	{
		tiltSpeed = TILT_SPEED_MAX;
	}
	else if (input<TILT_SPEED_MIN)
	{
		tiltSpeed = TILT_SPEED_MIN;
	}
	else
	{
		tiltSpeed = input;
	}
	
	menuValueSet(tiltSpeed,14);
	eeprom_update_byte (&tiltSpeedEep, tiltSpeed);
}



// *****************************************************************************
// Build platform variables and functions. *************************************
// *****************************************************************************
// Layer stuff.
uint8_t buildPlatformLayer = 36;		// See stuff file for calculations.
uint8_t buildPlatformBaseLayer = 10;
uint8_t buildPlatformLayerEep EEMEM;
uint8_t buildPlatformBaseLayerEep EEMEM;

// Movement stuff.
uint8_t buildPlatformSpeedEep EEMEM;
uint8_t buildPlatformSpeed = BUILDPLATFORM_SPEED_MIN;	// Actual value in init function from eeprom.

volatile int16_t buildTimerCompareValue = 8065;
volatile int16_t buildTimerTargetCompareValue = 8065;
#define BUILD_PLATFORM_TIMER_COMPARE_VALUE_MIN 1000
volatile uint8_t buildPlatformHomingFlag;

volatile uint8_t buildPlatformCount = 0;
// Position stuff.
// Position step is 0.01 mm (20 stepper steps).
volatile uint8_t stopFlag = 0;
volatile uint16_t buildPlatformPosition = 0;
volatile uint16_t buildPlatformTargetPosition = 0;
#define BUILDPLATFORM_TARGET_POSITION_MAX 40000


void buildPlatformEnableStepper(void)
{
	// Enable stepper.
	BUILDENABLEPORT |= (1 << BUILDENABLEPIN);
	_delay_ms(50); 		// Wait a bit until stepper driver is up and running.
	// Activate stepper timer clock source.
	TCCR1B |= (1 << CS10);
}

void buildPlatformUpwards(void)
{
	// Set upward direction.
	BUILDDIRPORT &= ~(1 << BUILDDIRPIN);
}

void buildPlatformDisableStepper(void)
{
	// Disable stepper driver.
	BUILDENABLEPORT &= ~(1 << BUILDENABLEPIN);
	// Deactivate stepper timer clock source.
	TCCR1B &= ~(1 << CS10);
}

void buildPlatformDownwards(void)
{
	// Set upward direction.
	BUILDDIRPORT |= (1 << BUILDDIRPIN);
}

// Adjust build platform drive speed. ******************************************
void buildPlatformAdjustSpeed (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if (++buildPlatformSpeed > BUILDPLATFORM_SPEED_MAX) buildPlatformSpeed = BUILDPLATFORM_SPEED_MAX;
	}
	// Decrease if input = 2.
	else if (input==2)
	{
		if (--buildPlatformSpeed < BUILDPLATFORM_SPEED_MIN) buildPlatformSpeed = BUILDPLATFORM_SPEED_MIN;
	}
	menuValueSet(buildPlatformSpeed,17);
// DO THIS DIRECTLY IN INTERRUPT.
//	buildTimerTargetCompareValue = buildPlatformSpeed * (-2621) + 10686;
//	if (buildTimerTargetCompareValue < BUILD_PLATFORM_TIMER_COMPARE_VALUE_MIN)	buildTimerTargetCompareValue = BUILD_PLATFORM_TIMER_COMPARE_VALUE_MIN;
	
	// Write to eeprom.
	eeprom_update_byte (&buildPlatformSpeedEep, buildPlatformSpeed);
}


// Adjust build platform drive speed. ******************************************
void buildPlatformSetSpeed (uint8_t input)
{
	// Increase if input = 1.
	if (input>=BUILDPLATFORM_SPEED_MAX)
	{
		buildPlatformSpeed = BUILDPLATFORM_SPEED_MAX;
	}
	// Decrease if input = 2.
	else if (input<BUILDPLATFORM_SPEED_MIN)
	{
		buildPlatformSpeed = BUILDPLATFORM_SPEED_MIN;
	}
	else
	{
		buildPlatformSpeed = input;
	}
	menuValueSet(buildPlatformSpeed,17);
	
	// Write to eeprom.
	eeprom_update_byte (&buildPlatformSpeedEep, buildPlatformSpeed);
}



// Adjust build platform position. *********************************************
void buildPlatformAdjustPosition (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if ((buildPlatformTargetPosition + buildPlatformLayer) > BUILDPLATFORM_TARGET_POSITION_MAX) buildPlatformTargetPosition = BUILDPLATFORM_TARGET_POSITION_MAX;
		else buildPlatformTargetPosition += buildPlatformLayer;
	}
	// Decrease if input = 2.
	else if (input==2)
	{	// Decrease, also check if position is below zero which means somewhere at > 60000 due to wrapping.
		if ((buildPlatformTargetPosition - buildPlatformLayer) == 0 || (buildPlatformTargetPosition - buildPlatformLayer) > 60000) buildPlatformTargetPosition = 0;
		else buildPlatformTargetPosition -= buildPlatformLayer;
	}
	menuValueSet(buildPlatformTargetPosition,20);
}



// Adjust build platform layer height. ********************************************
void buildPlatformAdjustLayerHeight (uint8_t input)
{
	if (input==1)
	{
		if (++buildPlatformLayer > BUILDPLATFORM_MAX_STANDARD_LAYERS) buildPlatformLayer = BUILDPLATFORM_MAX_STANDARD_LAYERS;
	}
	else if (input==2)
	{
		if (--buildPlatformLayer < 1) buildPlatformLayer = 1;
	}
	menuValueSet(buildPlatformLayer,18);
}

// Set build platform layer height. ********************************************
void buildPlatformSetLayerHeight (uint8_t input)
{
	if (input>=BUILDPLATFORM_MAX_STANDARD_LAYERS)
	{
		buildPlatformLayer = BUILDPLATFORM_MAX_STANDARD_LAYERS;
	}
	else if (input<1)
	{
		buildPlatformLayer = 1;
	}
	else
	{
		buildPlatformLayer = input;
	}
	
	menuValueSet(buildPlatformLayer,18);
	// Write to eeprom.
	eeprom_update_byte (&buildPlatformLayerEep, buildPlatformLayer);
}


// Adjust build platform base layer height. ************************************
void buildPlatformAdjustBaseLayerHeight (uint8_t input)
{
	if (input==1)
	{
		if (++buildPlatformBaseLayer > BUILDPLATFORM_MAX_STANDARD_LAYERS) buildPlatformBaseLayer = BUILDPLATFORM_MAX_STANDARD_LAYERS;
	}
	else if (input==2)
	{
		if (--buildPlatformBaseLayer < 1) buildPlatformBaseLayer = 1;
	}
	menuValueSet(buildPlatformBaseLayer,19);
}


// Set build platform base layer height. ***************************************
void buildPlatformSetBaseLayerHeight (uint8_t input)
{
	if (input>=BUILDPLATFORM_MAX_STANDARD_LAYERS)
	{
		buildPlatformBaseLayer = BUILDPLATFORM_MAX_STANDARD_LAYERS;
	}
	else if (input<1)
	{
		buildPlatformBaseLayer = 1;
	}
	else
	{
		buildPlatformBaseLayer = input;
	}
	
	menuValueSet(buildPlatformBaseLayer,19);
	// Write to eeprom.
	eeprom_update_byte (&buildPlatformBaseLayerEep, buildPlatformBaseLayer);
}


// Home build platform. Input is speed from 1 (slow) to 4 (fast). **************
void buildPlatformHome (void)
{
	// Start motor if not running already.
	if (!buildPlatformHomingFlag )//&& (LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))	//(!(TCCR3B & (1 << CS30)) && (LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))
	{
		buildPlatformHomingFlag = 1;
		buildPlatformTargetPosition = 0;
		menuValueSet(buildPlatformTargetPosition,20);
	}
	// Stop motor if running already.
	else
	{
//		LEDPORT ^= (1<<LEDPIN);
		stopFlag = 1;
		buildPlatformHomingFlag = 0;
	}
	if (!(LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))
	{
// TODO: Wait a bit to give control script a chance to listen for inputs.
		_delay_ms(100);
		printerOperatingFlag = 1;
	}
		
}



// Move build platform to top. Input is speed from 1 (slow) to 4 (fast). *******
void buildPlatformTop (void)
{
	if (!(TCCR1B & (1 << CS10)))	// If not running.
	{
		buildPlatformTargetPosition = BUILDPLATFORM_TARGET_POSITION_MAX;
//		menuValueSet(buildPlatformTargetPosition,20);
	}
	else
	{
		stopFlag = 1;
	}	
}



// Set target position for build platform. *************************************
void buildPlatformSetTarget(int16_t input)
{
	// Will position become negative?
	if ( (input < 0) && (abs(input) > buildPlatformTargetPosition) )
	{
		buildPlatformTargetPosition = 0;
	}
	else	// Just add...
	{
		buildPlatformTargetPosition += input;
	}
}



// Move layer up. **************************************************************
void buildPlatformLayerUp(void)
{
	// Increase build platform target position by layer height if smaller than max height.	
	if ((buildPlatformTargetPosition + buildPlatformLayer) > BUILDPLATFORM_TARGET_POSITION_MAX) buildPlatformTargetPosition = BUILDPLATFORM_TARGET_POSITION_MAX;
	else buildPlatformTargetPosition += buildPlatformLayer;
}



// Move base layer up. *********************************************************
void buildPlatformBaseLayerUp(void)
{
	if ((buildPlatformTargetPosition + buildPlatformBaseLayer) > BUILDPLATFORM_TARGET_POSITION_MAX) buildPlatformTargetPosition = BUILDPLATFORM_TARGET_POSITION_MAX;
	else buildPlatformTargetPosition += buildPlatformBaseLayer;
}



// Compare build platform current and target position. *************************
void buildPlatformComparePosition (uint8_t buildPlatformSpeed)
{
	// Calc timer compare value. *******************************************
	if (!(TCCR1B & (1 << CS10)))
	{
		buildTimerTargetCompareValue = buildPlatformSpeed * (-2621) + 10686;
		// Cap speed.
		if (buildTimerTargetCompareValue < BUILD_PLATFORM_TIMER_COMPARE_VALUE_MIN)	buildTimerTargetCompareValue = BUILD_PLATFORM_TIMER_COMPARE_VALUE_MIN;
		// Don't use target speed right from the start. Always start at lowest speed.
		buildTimerCompareValue = 8065;	// Reset only if not running.
		// Set timer compare value. Range between 202 and 8065, corresponding to 20 mm/s and 0.5 mm/s.
		timer1SetCompareValue(buildTimerCompareValue);
	}	


	// Move upwards.
	if (buildPlatformPosition < buildPlatformTargetPosition && !(TCCR1B & (1 << CS10)))
	{
		if (!(LIMITBUILDTOPPOLL & (1 << LIMITBUILDTOPPIN)))	// Check end switch (active high).
		{
			ledYellowOn();
			// Set upward direction.
			buildPlatformUpwards();
			
			// Enable stepper.
			buildPlatformEnableStepper();

		}
//		else printerOperatingFlag = 1;
	}
	// Move downwards. *****************************************************
	else if (buildPlatformPosition > buildPlatformTargetPosition && !(TCCR1B & (1 << CS10)))
	{
		if (!(LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))
		{
			ledGreenOn();
			// Set downward direction.
			buildPlatformDownwards();
			
			// Enable stepper.
			buildPlatformEnableStepper();
		}
//		else printerOperatingFlag = 1;
	}
	// Move to home position.
	// Only if homing flag set and motor not running.
	else if (buildPlatformHomingFlag && !(TCCR1B & (1 << CS10)))
	{
		// Home limit switch not active (low).
		if (!(LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))
		{
			ledGreenOn();
			// Set upward direction.
			buildPlatformDownwards();
			
			// Enable stepper.
			buildPlatformEnableStepper();
		}
		// Switch not pressed.
		else
		{
			buildPlatformPosition = 0;
		}
	}
}



// Control build platform movement. ********************************************		TO DO: ramping
void buildPlatformControl(void)
{
	// Adjust speed for ramping.
	if (buildTimerCompareValue > buildTimerTargetCompareValue)
	{
		buildTimerCompareValue -= 20;
		timer1SetCompareValue(buildTimerCompareValue);
	}

	// Adjust position every 20th step (every 0.01 mm). ********************
	// Upward direction.
	if (!(BUILDDIRPORT & (1 << BUILDDIRPIN)))
	{
		// Increment step counter every step.
		// Test if steps per standard layer are reached.
		if (++buildPlatformCount == BUILDPLATFORM_STEPS_PER_STANDARD_LAYER)
		{
			// Reset step counter
			buildPlatformCount = 0;
			// Deactivate stepper if target reached.
			buildPlatformPosition++;
			if (buildPlatformPosition == buildPlatformTargetPosition)
			{
				// Disable stepper.
				ledGreenOff();
				ledYellowOff();
				buildPlatformDisableStepper();
		//		menuChanged();
			}
			else if (buildPlatformPosition > buildPlatformTargetPosition)
			{
				// Reverse. 
				ledGreenOn();
				buildPlatformDownwards();//BUILDDIRPORT |= (1 << BUILDDIRPIN);
				menuChanged();
			}
		}
	}
	// Downward direction.
	else
	{
		// Increment step counter every step.
		if (++buildPlatformCount == BUILDPLATFORM_STEPS_PER_STANDARD_LAYER)
		{
			// Dont check if homing. Go until limit switch is hit.
			if (buildPlatformHomingFlag)
			{
				buildPlatformCount = 0;
				--buildPlatformPosition;
//				menuChanged();
			}
			else
			{
				// Reset step counter
				buildPlatformCount = 0;
				// Deactivate stepper if target reached.
				buildPlatformPosition--;
				if (buildPlatformPosition == buildPlatformTargetPosition)
				{
					ledGreenOff();
					ledYellowOff();
					buildPlatformDisableStepper();
				//	menuChanged();
				}
				else if (buildPlatformPosition < buildPlatformTargetPosition)
				{
					ledYellowOn();
					buildPlatformUpwards();
				//	menuChanged();
				}
			}
		}
	}
	
	// Stop on stop flag.
	if (stopFlag)
	{
		// Dectivate stepper timer clock source.
		ledYellowOff();
		ledGreenOff();
		buildPlatformDisableStepper();
		//TCCR1B &= ~(1 << CS10);
		// Reset stop flag.
		stopFlag = 0;
		// Set target position to current position.
		buildPlatformTargetPosition = buildPlatformPosition;
		menuValueSet(buildPlatformTargetPosition,20);
	//	menuChanged();
	}
}

/*

// *****************************************************************************
// Beamer platform variables and functions. ************************************
// *****************************************************************************

// Beamer platform variables. **************************************************
// Movement stuff.
uint8_t beamerSpeedEep EEMEM;
uint8_t beamerSpeed = BEAMER_SPEED_MIN;		// Get actual value in init function from eeprom.
volatile int16_t beamerTimerCompareValue = 8065;
volatile int16_t beamerTimerTargetCompareValue = 8065;
#define BEAMER_TIMER_COMPARE_VALUE_MIN 500


// Layer stuff.
uint8_t beamerLayer = 100;		// See stuff file for calculations. 1 = 0.01 mm
uint8_t beamerBaseLayer = 2;

// Positioning stuff
// Position step is 0.01 mm (20 stepper steps).
volatile uint16_t beamerPosition = 0;
volatile uint16_t beamerTargetPosition = 0;
volatile uint8_t beamerHomingFlag;
volatile uint8_t beamerCount = 0;
uint8_t beamerHiRes = 0;
uint8_t beamerHiResEep EEMEM;
uint16_t beamerHiResPosition;
uint16_t beamerLoResPosition;
uint16_t beamerHiResPositionEep EEMEM;
uint16_t beamerLoResPositionEep EEMEM;
volatile uint8_t beamerStopFlag = 0;
#define BEAMER_TARGET_POSITION_MAX 40000



// Adjust beamer drive speed. **************************************************
void beamerAdjustSpeed (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if (++beamerSpeed > BEAMER_SPEED_MAX) beamerSpeed = BEAMER_SPEED_MAX;
	}
	// Decrease if input = 2.
	else if (input==2)
	{
		if (--beamerSpeed < BEAMER_SPEED_MIN) beamerSpeed = BEAMER_SPEED_MIN;
	}
	menuValueSet(beamerSpeed,25);
// DO THIS DIRECTLY IN INTERRUPT.
//	beamerTimerTargetCompareValue = beamerSpeed * (-2621) + 10686;
//	if (beamerTimerTargetCompareValue < BEAMER_TIMER_COMPARE_VALUE_MIN)	beamerTimerTargetCompareValue = BEAMER_TIMER_COMPARE_VALUE_MIN;
	
	// Write to eeprom.
	eeprom_update_byte ( &beamerSpeedEep, beamerSpeed );
}



// Adjust build platform position. *********************************************
void beamerAdjustPosition (uint8_t input)
{
	// Increase if input = 1.
	if (input==1)
	{
		if ((beamerTargetPosition + beamerLayer) > BEAMER_TARGET_POSITION_MAX) beamerTargetPosition = BEAMER_TARGET_POSITION_MAX;
		else beamerTargetPosition += beamerLayer;
	}
	// Decrease if input = 2.
	else if (input==2)
	{	// Decrease, also check if position is below zero which means somewhere at > 60000 due to wrapping.
		if ((beamerTargetPosition - beamerLayer) == 0 || (beamerTargetPosition - beamerLayer) > 60000) beamerTargetPosition = 0;
		else beamerTargetPosition -= beamerLayer;
	}
	menuValueSet(beamerTargetPosition,27);
}



// Save hi resolution position. ************************************************
void beamerSetHiResPosition ( void )
{
	eeprom_update_word (&beamerHiResPosition, beamerPosition);
	beamerHiRes = 1;
	eeprom_update_byte (&beamerHiResEep, beamerHiRes);
}



// Save lo resolution position. ************************************************
void beamerSetLoResPosition ( void )
{
	eeprom_update_word (&beamerLoResPosition, beamerPosition);
	beamerHiRes = 0;
	eeprom_update_byte (&beamerHiResEep, beamerHiRes);
}



// Move to hi resolution position. ************************************************
void beamerMoveHiResPosition ( void )
{
//	if (!beamerHiRes)
//	{
		beamerTargetPosition = eeprom_read_word (&beamerHiResPosition);
		menuValueSet(beamerTargetPosition,27);
		beamerHiRes = 1;
		eeprom_update_byte (&beamerHiResEep, beamerHiRes);
//	}
}



// Move to lo resolution position. ************************************************
void beamerMoveLoResPosition ( void )
{
//	if (beamerHiRes)
//	{
		beamerTargetPosition = eeprom_read_word (&beamerLoResPosition);
		menuValueSet(beamerTargetPosition,27);
		beamerHiRes = 0;
		eeprom_update_byte (&beamerHiResEep, beamerHiRes);
//	}
}



// Home beamer. ****************************************************************
void beamerHome ( void )
{
	// Start motor if not running already.
	if (!beamerHomingFlag )//&& (LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))	//(!(TCCR3B & (1 << CS30)) && (LIMITBUILDBOTTOMPOLL & (1 << LIMITBUILDBOTTOMPIN)))
	{
//		LEDPORT &= ~(1<<LEDPIN);
		beamerHomingFlag = 1;
		beamerTargetPosition = 0;
		menuValueSet(beamerTargetPosition,27);
	}
	// Stop motor if running already.
	else
	{
//		LEDPORT |= (1<<LEDPIN);
		beamerStopFlag = 1;
		beamerHomingFlag = 0;
	}
	
	// Set operation flag to signal to printerReady() function.
	if (!(LIMITBEAMERBOTTOMPOLL & (1 << LIMITBEAMERBOTTOMPIN)))
	{
// TODO: Wait a bit to give control script a chance to listen for inputs.
		_delay_ms(100);
		printerOperatingFlag = 1;
	}
}


// Move beamer to bottom position. *********************************************
void beamerBottom ( void)
{
	if (!(TCCR1B & (1 << CS10)))	// If not running.
	{
		beamerTargetPosition = BEAMER_TARGET_POSITION_MAX;
		menuValueSet(beamerTargetPosition,27);
	}
	else
	{
		beamerStopFlag = 1;
	}	
}


// Compare beamer platform current and target position. *************************
void beamerComparePosition (uint8_t beamerSpeed)
{
	// Calc timer compare value. *******************************************
	if (!(TCCR1B & (1 << CS10)))
	{
		beamerTimerTargetCompareValue = beamerSpeed * (-2621) + 10686;
		if (beamerTimerTargetCompareValue < BEAMER_TIMER_COMPARE_VALUE_MIN)	beamerTimerTargetCompareValue = BEAMER_TIMER_COMPARE_VALUE_MIN;
		// Don't use target speed right from the start. Always start at lowest speed.
		beamerTimerCompareValue = 8065;	// Reset only if not running.
		// Set timer compare value. Range between 202 and 8065, corresponding to 20 mm/s and 0.5 mm/s.
		timer1SetCompareValue(beamerTimerCompareValue);
	}	


	// Move upwards.
	if (beamerPosition < beamerTargetPosition && !(TCCR1B & (1 << CS10)))
	{
		if (LIMITBEAMERTOPPOLL & (1 << LIMITBEAMERTOPPIN))	// Check end switch (active low).
		{
			// Enable stepper.
			BEAMERENABLEPORT &= ~(1 << BEAMERENABLEPIN);
			_delay_ms(50); 		// Wait a bit until stepper driver is up and running.
			// Set upward direction.
			BEAMERDIRPORT |= (1 << BEAMERDIRPIN);
			// Activate stepper timer clock source.
			TCCR1B |= (1 << CS10);
			LEDPORT ^= (1<<LEDPIN);
		}
	}
	// Move downwards. *****************************************************
	else if (beamerPosition > beamerTargetPosition && !(TCCR1B & (1 << CS10)))
	{
		if (LIMITBEAMERBOTTOMPOLL & (1 << LIMITBEAMERBOTTOMPIN))
		{
			// Enable stepper.
			BEAMERENABLEPORT &= ~(1 << BEAMERENABLEPIN);
			_delay_ms(50);
			// Set downward direction.
			BEAMERDIRPORT &= ~(1 << BEAMERDIRPIN);
			// Activate stepper timer clock source.
			TCCR1B |= (1 << CS10);
			
		}
	}
	// Move to home position.
	else if (beamerHomingFlag && !(TCCR1B & (1 << CS10)))
	{
		if (LIMITBEAMERBOTTOMPOLL & (1 << LIMITBEAMERBOTTOMPIN))
		{
			// Enable stepper.
			BEAMERENABLEPORT &= ~(1 << BEAMERENABLEPIN);
			_delay_ms(50);
			// Set downward direction.
			BEAMERDIRPORT &= ~(1 << BEAMERDIRPIN);
			// Activate stepper timer clock source.
			TCCR1B |= (1 << CS10);
			
		}
		else
		{
			beamerPosition = 0;
		}
	}
}



// Control beamer platform movement. ********************************************		TO DO: ramping
void beamerControl(void)
{
	// Adjust speed for ramping.
	if (beamerTimerCompareValue > beamerTimerTargetCompareValue)
	{
		if (beamerTimerCompareValue < 500)	beamerTimerCompareValue -= 5;
		else beamerTimerCompareValue -= 15;
		timer1SetCompareValue(beamerTimerCompareValue);
	}

	// Adjust position every 20th step (every 0.01 mm). ********************
	// Upward direction.
	if (BEAMERDIRPORT & (1 << BEAMERDIRPIN))
	{
		// Increment step counter every step.
		if (++beamerCount == BEAMER_STEPS_PER_STANDARD_LAYER)
		{
			// Reset step counter
			beamerCount = 0;
			// Deactivate stepper if target reached.
			beamerPosition++;
			if (beamerPosition == beamerTargetPosition)
			{
				TCCR1B &= ~(1 << CS10);	//BUILDENABLEPORT |= (1 << BUILDENABLEPIN);	
				menuChanged();
			}
			else if (beamerPosition > beamerTargetPosition)
			{
				BEAMERDIRPORT &= ~(1 << BEAMERDIRPIN);
				menuChanged();
			}
		}
	}
	// Downward direction.
	else
	{
		// Increment step counter every step.
		if (++beamerCount == BEAMER_STEPS_PER_STANDARD_LAYER)
		{
			// Dont check if homing. Go until limit switch is hit.
			if (beamerHomingFlag)
			{
				beamerCount = 0;
				--beamerPosition;
//				menuChanged();
			}
			else
			{
				// Reset step counter
				beamerCount = 0;
				// Deactivate stepper if target reached.
				beamerPosition--;
				if (beamerPosition == beamerTargetPosition)
				{
					TCCR1B &= ~(1 << CS10);	//BUILDENABLEPORT |= (1 << BUILDENABLEPIN);	
					menuChanged();
				}
				else if (beamerPosition < beamerTargetPosition)
				{
					BEAMERDIRPORT |= (1 << BEAMERDIRPIN);
					menuChanged();
				}
			}
		}
	}
	
	// Stop on stop flag.
	if (beamerStopFlag)
	{
		// Dectivate stepper timer clock source.
		TCCR1B &= ~(1 << CS10);
		// Reset stop flag.
		beamerStopFlag = 0;
		// Set target position to current position.
		beamerTargetPosition = beamerPosition;
		menuValueSet(beamerTargetPosition,27);
		menuChanged();
	}
}

*/

// *****************************************************************************
// Initialise printer function.*************************************************
// *****************************************************************************
void printerInit (void)
{

	lcd_gotoxy(0,0);
	lcd_puts("Initialising printer.");
	
	
	// Initialise values.
	tiltSpeed = 1;
	buildPlatformHomingFlag = 0;
	buildPlatformPosition = 0;
	buildPlatformTargetPosition = buildPlatformPosition;
//	beamerPosition = 0;
//	beamerTargetPosition = beamerPosition;

// TODO: Eeprom values get corrupted upon programming. Maybe read eeprom before programming and write file back into eeprom after programming.
	
	// Get values from eeprom.
	tiltSpeed = eeprom_read_byte (&tiltSpeedEep);
	tiltAngle = eeprom_read_byte (&tiltAngleEep);
	buildPlatformSpeed = eeprom_read_byte (&buildPlatformSpeedEep);
//	beamerSpeed = eeprom_read_byte (&beamerSpeedEep);
//	buildPlatformLayer = eeprom_read_byte (&buildPlatformLayerEep);
//	buildPlatformBaseLayer = eeprom_read_byte (&buildPlatformBaseLayerEep);
//	beamerHiRes = eeprom_read_byte(&beamerHiResEep);
//	beamerHiResPosition = eeprom_read_word (&beamerHiResPositionEep);
//	beamerLoResPosition = eeprom_read_word (&beamerLoResPositionEep);
	
	
	// Pass values to menu.
	menuValueSet(tiltAngle,13);
	menuValueSet(tiltSpeed,14);
	menuValueSet(buildPlatformSpeed,17);
	menuValueSet(buildPlatformLayer,18);
	menuValueSet(buildPlatformBaseLayer,19);
	menuValueSet(buildPlatformPosition,20);
//	menuValueSet(beamerSpeed,25);
//	menuValueSet(beamerTargetPosition,27);
//	menuValueSet(beamerHiResPosition,28);
//	menuValueSet(beamerLoResPosition,29);
	
	
/*
	// Run stepper drive loop and iterate through init steps.
	// 1: home beamer
	// 2: move beamer to position of chosen resolution.
	uint8_t initRunning = 0;
	uint8_t initStep = 1;
	while(initStep<3)
	{
		if (initStep==1 && initRunning==0)
		{
			// Home beamer platform.
			lcd_gotoxy(0,1);
			lcd_puts("Homing beamer");
			beamerHome();
			initRunning = 1;
		}
		else if (initStep==2 && initRunning==0)
		{
			lcd_gotoxy(0,1);
			if (beamerHiRes)
			{
				lcd_puts("Moving beamer to");
				lcd_gotoxy(0,2);
				lcd_puts(".05 mm res position");
				beamerMoveHiResPosition();
			}
			else
			{
				lcd_puts("Moving beamer to");
				lcd_gotoxy(0,2);
				lcd_puts("0.1 mm res position");
				beamerMoveLoResPosition();			
			}
			initRunning = 1;
		}
		
		// Check for difference between current and set build platform position.
		// Start stepper if difference detected.
		buildPlatformComparePosition(buildPlatformSpeed);
		beamerComparePosition(beamerSpeed);
		
		// Check if operation has finished an move to next step.
		if(printerReady())
		{
			initStep++;
			initRunning = 0;
		}
	}
	

	
	// Check for difference between current and set build platform position.
	// Start stepper if difference detected.
	buildPlatformComparePosition(buildPlatformSpeed);
	beamerComparePosition(beamerSpeed);

*/}


// *****************************************************************************
// Other functions. ************************************************************
// *****************************************************************************
// Function will return 1 ONCE after operation has finished.
uint8_t printerReady(void)
{
	// Just finished condition:
	// Tilt off, beamer platform off, build platform off?
	if( !(TCCR4B & (1 << CS43)) && !(TCCR1B & (1 << CS10)) && !(TCCR3B & (1 << CS30)) )
	{
		// Just finished: printerOperatingFlag is still 1.
		if (printerOperatingFlag)
		{
			printerOperatingFlag = 0;
			printerReadyFlag = 1;
		}
		// Finished some time ago: printerOperatingFlag 0 already.
		else
		{
			printerOperatingFlag = 0;
			printerReadyFlag = 0;		// TODO: was 0 but didnt make sense...
		}
	}
	// At least one of the motors running.
	else
	{
		printerOperatingFlag = 1;
		printerReadyFlag = 0;
	}
	return printerReadyFlag;
}

uint8_t printerGetState(void)
{
	return printerState;
}

void printerSetState(uint8_t input)
{
	printerState = input;
}

uint16_t printerGetSlice(void)
{
	return slice;
}

uint16_t printerGetNumberOfSlices(void)
{
	return numberOfSlices;
}

void printerSetSlice(uint16_t input)
{
	slice = input;
}

void printerSetNumberOfSlices(uint16_t input)
{
	numberOfSlices = input;
}

void disableSteppers(void)
{
	TILTENABLEPORT |= (1 << TILTENABLEPIN);
	BUILDENABLEPORT |= (1 << BUILDENABLEPIN);
//	BEAMERENABLEPORT |= (1 << BEAMERENABLEPIN);
}
//void tiltDisableStepper(void)
//{
//	TILTENABLEPORT |= (1 << TILTENABLEPIN);
//}
//void beamerDisableStepper(void)
//{
//	BEAMERENABLEPORT |= (1 <<BEAMERENABLEPIN);
//}
void buildDisableStepper(void)
{
	BUILDENABLEPORT |= (1 <<BUILDENABLEPIN);
}