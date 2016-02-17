#include <stdint.h>
#include <stdlib.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "ws2812.h"

#define T0H		400		// High time for 0 value, in ns
#define T1H		800		// High time for 1 value, in ns
#define Ttot	1250	// Total time for 1 bit, in ns

#define NS_PER_CYCLE	(1/F_CPU) * 1000000000
#define US_PER_CYCLE	(1/F_CPU) * 1000000
#define T0H_CYCLES T0H / NS_PER_CYCLE
#define T1H_CYCLES T1H / NS_PER_CYCLE
#define Ttot_CYCLES Ttot / NS_PER_CYCLE

/********************************************************************************
 * send_data
 *
 * Send out the bits of data, frame by frame. A frame sends out 1 bit, and looks
 * different for a 1 bit and a 0 bit:
 *
 * 1 bit:
 * +-----+-----+
 * |           |
 * |  A     B  |  C
 * +           +-----+
 *
 * A = 400ns
 * B = 400ns
 * C = 450ns
 *
 * 0 bit:
 * +-----+      
 * |     |      
 * |  A  |  B     C
 * +     +---- +-----+
 *
 * A = 400ns
 * B = 400ns
 * C = 450ns
 *
 * The most critical timing is A for a 0-bit. The least critical is C.
 *
 * We are running at 20MHz, so 1 processor cycle is 50ns. So A and B are 8 cycles
 * and C should be 9.
 *
 * The implementation of this is heavily inspired by cpucpld's light_ws2812 
 * library at https://github.com/cpldcpu/light_ws2812/
 ********************************************************************************/

static void send_data(uint8_t *framebuffer, uint16_t data_length, uint8_t data_pin)
{

	uint8_t current_byte;
	uint8_t i;

	cli();

	uint8_t high_value = PORTB | (1 << data_pin);
	uint8_t low_value = PORTB & ~(1 << data_pin);


	// Outer loop & first statement take 7 cycles.

	while ( data_length--) {				// 5

		// Fetch next byte. 

		current_byte = *framebuffer++;		// 2

		// Push out 8 bits.
		  
		asm volatile (
			"	ldi %[loopcounter], 8 	\n\t" 	// 1 - 24
			" loop: "
			"	out %[port], %[high]	\n\t" 	// 1 - 25 

		// Phase A
		
			"	nop			\n\t"	// 1 - 1
			"	nop			\n\t"	// 1 - 2
			"	nop			\n\t"	// 1 - 3
			"	nop			\n\t"	// 1 - 4
			"	nop			\n\t"	// 1 - 5
			"	lsl %[data]		\n\t"	// 1 - 6
			"	brcs bit_is_1		\n\t"	// False: 1 - 7, True 2 - 8
			"	out %[port], %[low]	\n\t"	// 1 - 8

		// Phase B, entered for a 0 bit. 

			"       rjmp bit_is_0		\n\t"	// 2 - 10

		// Phase B, entered for a1 bit.

			" bit_is_1:			\n\t"
			"	nop			\n\t"   // 1 - 9
			"	nop			\n\t"   // 1 - 10

		// Phase B, entered for both 0 and 1 bit.

			" bit_is_0:			\n\t"
			"	nop			\n\t"   // 1 - 11
			"	nop			\n\t"	// 1 - 12
			"	nop			\n\t"	// 1 - 13
			"	nop			\n\t"	// 1 - 14
			"	nop			\n\t"	// 1 - 15
			"	out %[port], %[low]	\n\t" 	// 1 - 16


		// Phase C. 

			"	dec %[loopcounter]	\n\t"	// 1 - 17
			"	breq new_byte		\n\t"	// False: 1 - 18, True: 2 - 19

		// Still bits left to send in this byte - stay in inner loop

			"	nop			\n\t"	// 1 - 19
			"	nop			\n\t"	// 1 - 20
			"	nop			\n\t"	// 1 - 21
			"	nop			\n\t"	// 1 - 22
			"	rjmp loop		\n\t"	// 2 - 24

		// New byte needed. Fall out of inner loop

			" new_byte:			\n\t"
			
			: [loopcounter] "=&d" (i) 			
			: [data] "r" (current_byte), [port] "I" (_SFR_IO_ADDR(PORTB)), [high] "r" (high_value), [low] "r" (low_value)
		);
    
	}

	sei();

}

/************************************************************
 * send_frame: sends a frame of data out
 *	Params:
 *		struct RGB * 	LED color data
 *		uint8_t 		number of LEDs
 *		uint8_t			data pin
 *	Returns:
 *		void
 * 
 * This just send out led_data (struct RGB *) out, as the 
 * layout of led_data in memory is already correct for the
 * WSB2812 protocol (GRB, MSB first). The GRB part is taken
 * care of by the definition of struct RGB, and the MSB first
 * is taken care of by the lsl shift in send_data
 ************************************************************/


extern void send_frame(struct RGB *led_data, uint8_t num_leds, uint8_t data_pin)
{

	// Set data pin low
	DDRB |= (1 << data_pin);
	PORTB &= ~(1 << data_pin);

	// Send out data
	send_data((uint8_t *)led_data, num_leds * 3, data_pin);

}
