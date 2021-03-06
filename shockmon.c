/*
 * shockmon.c
 *
 *  Created on: 22 Feb 2014
 *      Author: Chinemelu Ezeh
 *
 *      Shock monitor listens for changes in the accelerometer.
 *      If there isi a change by a certain measure, the whole system
 *      stops and the accelerometer waveform is logged.
 */

#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "driverlib/fpu.h"
#include "driverlib/sysctl.h"
#include "driverlib/rom.h"
#include "driverlib/pin_map.h"
#include "driverlib/uart.h"
#include "grlib/grlib.h"
#include "drivers/cfal96x64x16.h"
#include "utils/uartstdio.h"
#include "driverlib/gpio.h"
#include "driverlib/systick.h"
#include "driverlib/interrupt.h"
#include "utils/ustdlib.h"
#include "driverlib/timer.h"
#include "inc/hw_timer.h"
#include "inc/hw_nvic.h"
#include "inc/hw_types.h"
#include "inc/hw_types.h"
#include "inc/hw_ints.h"
#include "inc/hw_gpio.h"
#include "driverlib/debug.h"
#include "driverlib/adc.h"
#include "exercise2.h"
#include "uicontrol.h"
#include "acquire.h"


volatile uint32_t puiADC1Buffer[1];
volatile uint32_t prev1_value;
volatile bool first_entry = true;
volatile tuiConfig* psuiConfig;
#define MAX_BLINK_COUNT 50

//*****************************************************************************
//
// Flags that contain the current value of the interrupt indicator as displayed
// on the CSTN display.
//
//*****************************************************************************
uint32_t g_ui32Flags;
volatile uint8_t led_val = 0;
volatile uint32_t blink_count;

void MonitorStart();

void LEDToggleISR() {
	ROM_TimerIntClear(TIMER2_BASE, TIMER_TIMA_TIMEOUT);
	//
	// Toggle the flag for the third timer.
	//
	HWREGBITW(&g_ui32Flags, 2) ^= 1;
//	HWREGBITW(GPIO_PORTG_BASE, 2) ^= 1;
	if (led_val == 255) {
		led_val = 0;
	} else {
		led_val = 255;
	}
	GPIOPinWrite(GPIO_PORTG_BASE,0x04, led_val);
	blink_count++;
	if (blink_count == MAX_BLINK_COUNT){
		ROM_TimerDisable(TIMER2_BASE, TIMER_A);
		// Start monitoring again
		blink_count = 0;
		MonitorStart();
	}
}

void
MonitorStart() {
	// Let the ISR know that this is the first entry
	// Important, can cause false shock detection
	// bug if omitted.
	first_entry = true;

	ROM_TimerEnable(TIMER1_BASE, TIMER_A);
	//
	// Disable ADC sequencers
	//
	ROM_ADCSequenceEnable(ADC1_BASE, 3);
}

void
MonitorStop() {
	ROM_TimerDisable(TIMER1_BASE, TIMER_A);
	//
	// Disable ADC sequencers
	//
	ROM_ADCSequenceDisable(ADC1_BASE, 3);
}
/****************************************************************
 * This interrupt handler is set up by AcquireInit to check
 * for the events that begin a data logging. This is when the voltage
 * exceeds a changeable threshold or
 * the accelerometer z-axis changes g by more than 0.5.
 *****************************************************************/
void MonitorShockISR() {
    ROM_TimerIntClear(TIMER1_BASE, TIMER_TIMA_TIMEOUT);
    //
    // Toggle the flag for the second timer.
    //
    HWREGBITW(&g_ui32Flags, 1) ^= 1;
	// If ADC has not converted yet, exit ISR.
   	if (!ADCIntStatus(ADC1_BASE, 3, false)) {
   		return;
   	}
   	ADCIntClear(ADC1_BASE, 3);
	ADCSequenceDataGet(ADC1_BASE, 3, puiADC1Buffer);
	ADCProcessorTrigger(ADC1_BASE, 3);
	if (first_entry) {
		first_entry = false;
		prev1_value = ReadAccel(puiADC1Buffer[0]);
	} else {
		puiADC1Buffer[0] = ReadAccel(puiADC1Buffer[0]);
		// Shock monitor detects a change of more than 2.4g
		if (abs((int)puiADC1Buffer[0] - prev1_value) > 240) {
//			UARTprintf("Shock! : %d \r",puiADC1Buffer[0]);
			MonitorStop();
			// Start LED
			ROM_TimerEnable(TIMER2_BASE, TIMER_A);
			// Start logging waveform.
			ROM_IntMasterDisable();
			psuiConfig->isShocked = true;
			ROM_IntMasterEnable();
		} else {
			ROM_IntMasterDisable();
			psuiConfig->isShocked = false;
			ROM_IntMasterEnable();
		}
		prev1_value = puiADC1Buffer[0];
	}
}

/****************************************************************
 * This function should be called first in AcquireRun.
 * It starts the ADC interrupt to detect the beginning of data logging
 * which is when the voltage exceeds a changeable threshold or
 * the accelerometer z-axis changes g by more than 0.5. The trigger
 * to look out for depends on the user's selection. The interrupt is
 * called every 100ms.
 ****************************************************************/
void ADC1AcquireStart() {

	//
	// Enable sample sequence 3 with a timer trigger.  Sequence 3
	// will do a single sample when the processor sends a signal to start the
	// conversion.  Each ADC module has 4 programmable sequences, sequence 0
	// to sequence 3.
	//
	ADCSequenceConfigure(ADC1_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);

	//
	// Configure step 0 on sequence 3.  Sample channel 0 (ADC_CTL_CH0) in
	// single-ended mode (default) and configure the interrupt flag
	// (ADC_CTL_IE) to be set when the sample is done.  Tell the ADC logic
	// that this is the last conversion on sequence 3 (ADC_CTL_END).  Sequence
	// 3 has only one programmable step.  Sequence 1 and 2 have 4 steps, and
	// sequence 0 has 8 programmable steps.  Since we are only doing a single
	// conversion using sequence 3 we will only configure step 0.  For more
	// information on the ADC sequences and steps, reference the datasheet.
	//

	ADCSequenceStepConfigure(ADC1_BASE, 3, 0, ADC_CTL_CH21 |ADC_CTL_IE|
			ADC_CTL_END);
	//
	// Since sample sequence 3 is now configured, it must be enabled.
	//
	ADCSequenceEnable(ADC1_BASE, 3);

	//
	// Clear the interrupt status flag.  This is done to make sure the
	// interrupt flag is cleared before we sample.
	//
	ADCIntClear(ADC1_BASE, 3);
	// Enable the interrupt after calibration.
//	ADCIntEnable(ADC1_BASE, 3);
}

void InitialiseShockLED() {
	//
	    // Enable the GPIO port that is used for the on-board LED.
	    //
		SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);
		GPIOPinTypeGPIOOutput(GPIO_PORTG_BASE, GPIO_PIN_2);

}

void MonitorShockInit( tuiConfig* psuiConfig_t) {
	psuiConfig = psuiConfig_t;
	led_val = 0;
	blink_count = 0;

	// Initialise LED for shock indication
	InitialiseShockLED();
	//Initialise timer to control led

	ConfigTimer2(SysCtlClockGet()/5, LEDToggleISR);

	ADC1AcquireStart();
	//Need to poll the ADC
	ADCProcessorTrigger(ADC1_BASE, 3);
	//Configure timer for ADC shock monitor
	ConfigTimer1(SysCtlClockGet()/100, MonitorShockISR); //10ms
	//Start Monitor
	MonitorStart();
 }

