/************************************************
 * ws2812.h
 *
 * WS2812 LED driver library
 ************************************************/

/* Public interface */

/************************************************************
* struct RGB: color data for 1 LED. 8-bit color
*************************************************************/

struct RGB {
	uint8_t green;
	uint8_t red;
	uint8_t blue;
};

/************************************************************
 * send_frame: sends a frame of data out
 *	Params:
 *		struct RGB * 	LED color data
 *		uint8_t 		number of LEDs
 *		uint8_t			data pin
 *	Returns:
 *		void
 ************************************************************/

extern void send_frame(struct RGB *, uint8_t, uint8_t);
