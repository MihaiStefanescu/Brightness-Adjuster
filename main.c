#include <avr/io.h>
#include <string.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "usbdrv.h"

#define F_CPU 16000000L
#include <util/delay.h>

#define USB_LED_OFF 0
#define USB_LED_ON  1
#define USB_MEASURE_DISTANCE 2

long k = 0;
static int last_dist = 0;

static uchar replyBuf[16] = "Hello, USB!";
static uchar dataReceived = 0, dataLength = 0; // for USB_DATA_IN

// microsecs.
uint16_t getPulseWidth()
{
	uint32_t i,result;

	//Wait for the rising edge
	for(i=0;i<600000;i++)
	{
		if(!(PINB & (1<<PB1))) continue; else break;
	}

	if(i==600000)
		return 0xffff; //Indicates time out

	//High Edge Found

	//Setup Timer1
	TCCR1A=0X00;
	TCCR1B=(1<<CS11); //Prescaler
	TCNT1=0x00;       //Init counter

	//Now wait for the falling edge
	for(i=0;i<600000;i++)
	{
		if(PINB & (1<<PB1))
		{
			if(TCNT1 > 60000) break; else continue;
		}
		else
			break;
	}

	if(i==600000)
		return 0xffff; //Indicates time out

	//Falling edge found

	result = TCNT1;

	//Stop Timer
	TCCR1B=0x00;

	if(result > 60000)
		return 0xfffe; //No obstacle
	else
		return (result>>1);
}

// this gets called when custom control message is received
USB_PUBLIC uchar usbFunctionSetup(uchar data[8]) {
	uint16_t r;
	usbRequest_t *rq = (void *)data; // cast data to correct type

	switch(rq->bRequest) { // custom command is in the bRequest field
		case USB_LED_ON:
			PORTD |= (1 << PD7); // turn LED on
			return 0;
		case USB_LED_OFF: 
			PORTD &= ~(1 << PD7); // turn LED off
			return 0;
		case USB_MEASURE_DISTANCE:
			_delay_ms(50);
			
			PORTB &= ~(1<<PB0);	//Trigger low
			
			_delay_us(10); 

			PORTB |= (1<<PB0);  //Trigger high
			_delay_us(15);     //For 50 us
			PORTB &= ~(1<<PB0);//Trigger low
			_delay_us(20);

			r = getPulseWidth();

			last_dist = r / 58.0; // in cm

			replyBuf[0] = last_dist / 1000 + 48;
			replyBuf[1] = (last_dist % 1000) / 100 + 48;
			replyBuf[2] = (last_dist % 100) / 10 + 48;
			replyBuf[3] = (last_dist % 10) + 48;
			replyBuf[4] = '\0';
			usbMsgPtr = replyBuf;
			return sizeof(replyBuf);
			return 0;
	}

	return 0; // should not get here
}

USB_PUBLIC uchar usbFunctionWrite(uchar *data, uchar len) {
	uchar i;

	for(i = 0; dataReceived < dataLength && i < len; i++, dataReceived++)
		replyBuf[dataReceived] = data[i];

	return (dataReceived == dataLength); // 1 if we received it all, 0 if not
}

int main() {
	uchar i;

	/* Led init. */
	DDRD |= (1 << PD7);
	/*----------------*/

	// Prox. sensor init. 
	DDRB  |= (1 << PB0); //Trigger
	DDRB  &= ~(1 << PB1); //Collector
	//Input pin but not pulled-up as it is driven by 'Echo'
	//---------------------------------


	wdt_enable(WDTO_1S); // enable 1s watchdog timer

	usbInit();

	usbDeviceDisconnect(); // enforce re-enumeration
	for(i = 0; i<250; i++) { // wait 500 ms
		wdt_reset(); // keep the watchdog happy
		_delay_ms(2);
	}
	usbDeviceConnect();

	sei(); // Enable interrupts globally, after re-enumeration

	while(1) {
		wdt_reset(); // keep the watchdog happy
		usbPoll();
	}

	return 0;
}
