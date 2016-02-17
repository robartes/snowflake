#define	F_CPU	20000000UL

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "ws2812.h"

#define	NUM_LEDS			18
#define	NUM_PATTERNS		23
#define FRAME_DELAY			16	// Number of milliseconds between frames
#define COLOUR_FLASH_COUNT	6	// Speed of flashing rainbow (number of frames between halving steps)
#define COLOUR_WALK_COUNT 	15	// Number of frames between moves of walking colours
#define FADE_DELAY 			3	// Number of frames between successive steps of a fade down
#define DEBOUNCE_COUNT_SHORT	10	// Number of 10ms slices after which a button press is registered as a short push
#define DEBOUNCE_COUNT_LONG		100	// Number of 10ms slices after which a button press is registered as a long push
#define DEBOUNCE_COUNT_MID		(DEBOUNCE_COUNT_LONG - DEBOUNCE_COUNT_SHORT) / 2 
#define DEMO_TIME_COUNT		500  // Number of 10ms slices between demo mode pattern switches

#define TRILOBE_INITIAL_STATE	0b00111000
#define TRICIRCLE_INITIAL_STATE	0b00001000

#define LED_PIN		PB0
#define BUTTON		PB1

#define IS_BIT_SET(var, pos) ((var) & (1<<(pos)))

#define NUM_QUICK_FLASH		3	// Number of times to flash LEDS in quick flash
#define QUICK_FLASH_DELAY   25 // Time in ms between steps of quick flash

enum {
	COLOUR_TYPE_COLD,
	COLOUR_TYPE_WARM,
};

enum {
	SINGLE_COLOUR_RED,
	SINGLE_COLOUR_GREEN,
	SINGLE_COLOUR_BLUE,
	SINGLE_COLOUR_RANDOM,
};

/******************************************************************
 * Utility functions
 ******************************************************************/

/******************************************************************
 * random: generate a random 8-bit number
 *
 * Parameters:
 * 		none
 * Returns:
 * 		uint8_t 	random value
 ******************************************************************/
static uint8_t random_byte(void) 
{
	
	uint8_t result = rand();

	return result >> 2;

}

/******************************************************************
 * fill_rainbow_colours: Generate rainbow colours
 *
 * Parameters:
 * 		uint8_t num_leds	Number of leds to generate
 * 					colours for
 *		struct RGB *led_data	The LED data to fill in
 * Returns:
 * 		void
 *
 * Intermediate colour generation could be done with sines for an
 * arbitrary number of colours, but I'm using a lookup table for up
 * to 18 LEDs (and 3 * 5 intermediate colours) here. More than 18
 * LEDs simply repeat the 18 LED sequence
 *
 * 15-LED example (num_intermediate_colours = 4)
 * Colours	R . . . . G . . . . B . . . .
 * Phases	P i i i i P i i i i P i i i i
 *
 * P = primary colour, reset phase
 * i = intermediate colour
 ******************************************************************/

void fill_rainbow_colours(struct RGB *led_data, uint8_t num_leds) 
{

	/* Colour lookup tables. */ 
	uint8_t intermediate_colours_2[2] = { 127, 63 };
	uint8_t intermediate_colours_3[3] = { 127, 85, 42};
	uint8_t intermediate_colours_4[4] = { 127, 95, 63, 32 };
	uint8_t intermediate_colours_5[5] = { 127, 102, 76, 51, 26};

	uint8_t *intermediate_colours[4] = { 
		intermediate_colours_2,
		intermediate_colours_3,
		intermediate_colours_4,
		intermediate_colours_5,
	};

	uint8_t num_rainbow = (num_leds > 18) ? 18 : num_leds;
	uint8_t num_intermediate_colours = (num_rainbow / 3);
	uint8_t i;
	uint8_t current_colour = 0;
	uint8_t current_triplet[3] = {0, 0, 0};

	
	for (i = 0; i < num_leds; i++) {

		uint8_t phase = i % num_intermediate_colours;

		if (phase == 0) {
		
			// First phase: 1 colour is 255, the rest is 0
			current_triplet[0] = current_triplet[1] = current_triplet[2] = 0;
			current_triplet[current_colour] = 127;

	} else {

			// Intermediate phases: lookup colours in lookup table
			current_triplet[current_colour] = intermediate_colours[num_intermediate_colours - 3][phase - 1];
			current_triplet[(current_colour+1)%3] = intermediate_colours[num_intermediate_colours - 3][num_intermediate_colours - phase - 1];
			

		}

		led_data[i].red = current_triplet[0];
		led_data[i].green = current_triplet[1];
		led_data[i].blue = current_triplet[2];

		// Go to next colour if necessary
		if (phase == num_intermediate_colours - 1) {
			current_colour++;
		}

	}


}

/******************************************************************
 * rotate_right: rotate LED data one step to the right
 *
 * Parameters:
 *		struct RGB *data	LED data
 * 		uint8_t num_leds	Number of LEDs
 ******************************************************************/
 
static void rotate_right(struct RGB *data, uint8_t num_leds)
{

	uint8_t i = 0;

	struct RGB temp = data[num_leds - 1];

	for (i = num_leds - 1; i > 0 ; i--) {
		data[i] = data[i - 1];
	}

	data[0] = temp;

}

/******************************************************************
 * intensity_halve: halve intensity
 *
 * Parameters:
 *		struct RGB *data	LED data
 * 		uint8_t num_leds	Number of LEDs
 *
 * Returns:
 * 		uint8_t	remaining_colours Number of colors with re-
 * 					maining intensity 
 ******************************************************************/

static uint8_t intensity_halve(struct RGB *data, uint8_t num_leds) 
{

	uint8_t i;
	uint8_t remaining_colours = 0;

	for (i = 0 ; i < num_leds; i++) {
		data[i].red >>= 1;
		if (data[i].red) {remaining_colours++;}
		data[i].green >>= 1;
		if (data[i].green) {remaining_colours++;}
		data[i].blue >>= 1;
		if (data[i].blue) {remaining_colours++;}
	}

	return remaining_colours;
}

/******************************************************************
 * fill_colours: fill with warm or cold colours
 *
 * Parameters:
 *		struct RGB *data	LED data
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t colour_type	Colour type
 ******************************************************************/

static void fill_colours(struct RGB *data, uint8_t num_leds, uint8_t colour_type)
{

	uint8_t i;
	uint8_t index_red = 0;
	uint8_t rgb_values[3][3] = {
		{ 128, 0, 0},
		{ 76, 76, 0},
		{ 102, 51, 0}, 
	};

	switch(colour_type) {

		case COLOUR_TYPE_WARM:
			index_red = 0;
			break;

		case COLOUR_TYPE_COLD:
			index_red = 2;
			break;
	}

	// Deal with idiosyncratic wiring
	data[0].red = rgb_values[1][index_red];
	data[0].green = rgb_values[1][1];
	data[0].blue = rgb_values[1][2 - index_red];
	data[1].red = rgb_values[0][index_red];
	data[1].green = rgb_values[0][1];
	data[1].blue = rgb_values[0][2 - index_red];


	for (i = 2; i < num_leds; i++) {


		data[i].red = rgb_values[i % 3][index_red];
		data[i].green = rgb_values[i % 3][1];
		data[i].blue = rgb_values[i % 3][2 - index_red];

	}
}

/******************************************************************
 * fill_range_colour: fill a range of LEDs with a colour
 *
 * Parameters:
 *		struct RGB *data	LED data
 *		uint8_t start		First led
 * 		uint8_t end			Last led (will also be filled)
 *		struct RGB colour	colour
 *
 * Please note: no boundary checking
 ******************************************************************/

static void fill_range_colour(struct RGB *data, uint8_t start, uint8_t end, struct RGB colour)
{

	uint8_t i;

	for (i = start; i <= end; i++) {
		data[i].red = colour.red;
		data[i].green = colour.green;
		data[i].blue = colour.blue;
	}

}

/******************************************************************
 * fill_some_colour: fill a number of LEDs with a colour
 *
 * Parameters:
 *		struct RGB *data	LED data
 *		uint8_t num_leds	Number of LEDs to fill
 *		uint8_t *leds		LEDs to fill
 *		struct RGB colour	colour
 *
 * Please note: no boundary checking
 ******************************************************************/

static void fill_some_colour(struct RGB *data, uint8_t num_leds, uint8_t *leds, struct RGB colour)
{

	uint8_t i;

	for (i = 0; i < num_leds; i++) {
		data[leds[i]].red = colour.red;
		data[leds[i]].green = colour.green;
		data[leds[i]].blue = colour.blue;
	}

}

/******************************************************************
 * get_colour_from_parameter: build RGB from colour_type param
 *
 * Parameters:
 *		uint8_t colour_type
 *
 * Returns: 
 *		struct RGB
 ******************************************************************/

static struct RGB get_colour_from_parameter(uint8_t colour_type)
{

	uint8_t i;
	struct RGB colour;

	uint8_t random_triplet[3];

	if ( colour_type == SINGLE_COLOUR_RANDOM) {
		for (i = 0; i < 3; i++) {
			random_triplet[i] = random_byte();
		}
	}

	switch (colour_type) {

		case SINGLE_COLOUR_RED:
			colour = (struct RGB){ 0, 0x80, 0 };  // The RHS is a 'Compound Literal'. Google it
		break;

		case SINGLE_COLOUR_GREEN:
			colour = (struct RGB){ 0x60, 0, 0 }; // 0x60 to equalise brightness. Human eye is more sensitive to green
		break;

		case SINGLE_COLOUR_BLUE:
			colour = (struct RGB) { 0, 0, 0x80 };
		break;

		case SINGLE_COLOUR_RANDOM:
			colour.red = random_triplet[0];
			colour.green = random_triplet[1];
			colour.blue = random_triplet[2];
		break;
	}


	return colour;

}

/******************************************************************
 * Pattern function definitions
 *
 * These are called by the main loop to generate the data every
 * 1/60 s. I am using (void *) type to pass a parameter to a pattern
 * function for future flexibility, even though I am currently only 
 * using it as a uint8_t
 ******************************************************************/

static uint8_t fill_single_colour(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t rainbow(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t rainbow_wheel(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t fade_colours(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t crazy(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t walking_colour(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t walking_bar(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t trilobe(struct RGB *, uint8_t, uint8_t, void *);
static uint8_t tricircle(struct RGB *, uint8_t, uint8_t, void *);

static uint8_t fade_down(struct RGB *, uint8_t, uint8_t, void *);

struct patternfunc {
	uint8_t (*run_pattern) (struct RGB *, uint8_t, uint8_t, void *);
	void *extra_parameter;
};

struct colour_param {
	uint8_t *counter;
	uint8_t colour_type;
};

struct trilobe_param {
	uint8_t *counter;
	uint8_t *state;
};

uint8_t trilobe_initial_state = TRILOBE_INITIAL_STATE;

struct tricircle_param {
	uint8_t *counter;
	uint8_t *state;
	uint8_t colour_type;
};

uint8_t tricircle_initial_state = TRICIRCLE_INITIAL_STATE;

uint8_t pattern_counter = 0;
struct colour_param fcp_cold = {&pattern_counter, COLOUR_TYPE_COLD};
struct colour_param fcp_warm = {&pattern_counter, COLOUR_TYPE_WARM};
struct colour_param wcp_red = {&pattern_counter, SINGLE_COLOUR_RED};
struct colour_param wcp_green = {&pattern_counter, SINGLE_COLOUR_GREEN};
struct colour_param wcp_blue = {&pattern_counter, SINGLE_COLOUR_BLUE};
struct colour_param wcp_random = {&pattern_counter, SINGLE_COLOUR_RANDOM};
struct trilobe_param tcp = {&pattern_counter, &trilobe_initial_state};
struct tricircle_param tccp_red = {&pattern_counter, &trilobe_initial_state, SINGLE_COLOUR_RED};
struct tricircle_param tccp_green = {&pattern_counter, &trilobe_initial_state, SINGLE_COLOUR_GREEN};
struct tricircle_param tccp_blue = {&pattern_counter, &trilobe_initial_state, SINGLE_COLOUR_BLUE};
struct tricircle_param tccp_random = {&pattern_counter, &trilobe_initial_state, SINGLE_COLOUR_RANDOM};

/*** Pattern table. Patterns are cycled through this consecutively ***/
struct patternfunc pattern_functions[NUM_PATTERNS] = {
	{fill_single_colour,(void *) SINGLE_COLOUR_RED },	
	{fill_single_colour,(void *) SINGLE_COLOUR_GREEN },	
	{fill_single_colour,(void *) SINGLE_COLOUR_BLUE },	
	{fill_single_colour,(void *) SINGLE_COLOUR_RANDOM },	
	{rainbow, (void *) 0 },	
	{rainbow, (void *) &pattern_counter },	
	{rainbow_wheel, (void *) &pattern_counter },	
	{fade_colours, (void *) &fcp_cold },	
	{fade_colours, (void *) &fcp_warm },	
	{crazy, (void *) &pattern_counter },	
	{walking_colour, (void *) &wcp_red },	
	{walking_colour, (void *) &wcp_green },	
	{walking_colour, (void *) &wcp_blue },	
	{walking_colour, (void *) &wcp_random },	
	{walking_bar, (void *) &wcp_red },	
	{walking_bar, (void *) &wcp_green },	
	{walking_bar, (void *) &wcp_blue },	
	{walking_bar, (void *) &wcp_random },	
	{trilobe, (void *) &tcp },	
	{tricircle, (void *) &tccp_red },	
	{tricircle, (void *) &tccp_green },	
	{tricircle, (void *) &tccp_blue },	
	{tricircle, (void *) &tccp_random },	
};

/*** Status codes ***/

enum {
	PATTERN_STATUS_NEW,
	PATTERN_STATUS_REFRESH,
	PATTERN_STATUS_NOCHANGE,
	PATTERN_STATUS_FADE_DONE,
	PATTERN_STATUS_SHORT_FLASH,
};


/*******************************************************************
 * Pattern functions themselves
 *
 * The status parameter of each has the following meaning:
 * 		0 - First time this pattern is run 
 * 		1 - Frame needs to be sent
 * 		2 - No change to data
 * 		>2 - pattern specific meaning, is passed into the next call
 *******************************************************************/

/******************************************************************
 * fade_down: fade down to zero intensity
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		uint8_t count 		Running counter
 *
 * This is a special pattern function that is called between tran-
 * sitions between the other pattern functions. It fades the LEDs
 * down to zero intensity
 *******************************************************************/

static uint8_t fade_down (struct RGB *data, uint8_t num_leds, uint8_t status, void *count)
{

	uint8_t new_status = PATTERN_STATUS_REFRESH;
	uint8_t *counter = (uint8_t *) count;

	switch (status) {

		case PATTERN_STATUS_NEW:
			*counter = 0;
			intensity_halve(data, num_leds);
			new_status = PATTERN_STATUS_REFRESH;
			break;

		default:
			(*counter)++;
			break;

	}

	if ( *counter == FADE_DELAY) {

		*counter = 0;
		
		if (intensity_halve(data, num_leds)) {
			new_status = PATTERN_STATUS_REFRESH;
		} else {
			new_status = PATTERN_STATUS_FADE_DONE;
		}

	}

	return new_status;

}



/******************************************************************
 * Single color - show a single color
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		uint8_t mode 		Color
 * 				0 	-	Red
 * 				1	-	Green
 * 				2	-	Blue
 * 				3	-	Random color
 ******************************************************************/

static uint8_t fill_single_colour(struct RGB *data, uint8_t num_leds, uint8_t status, void *mode)
{

	uint8_t i;
	uint8_t random_triplet[3];

	if ( (int) mode == SINGLE_COLOUR_RANDOM) {
		for (i = 0; i < 3; i++) {
			random_triplet[i] = random_byte();
		}
	}

	if ( status == PATTERN_STATUS_NEW ) {
		status = PATTERN_STATUS_REFRESH;
		for(i = 0; i < num_leds; i++) {

			switch ((int) mode) {

				case SINGLE_COLOUR_RED:
					data[i] = (struct RGB){ 0, 0x80, 0 };  // The RHS is a 'Compound Literal'. Google it
				break;

				case SINGLE_COLOUR_GREEN:
					data[i] = (struct RGB){ 0x60, 0, 0 }; // 0x60 to equalise brightness. Human eye is more sensitive to green
				break;

				case SINGLE_COLOUR_BLUE:
					data[i] = (struct RGB) { 0, 0, 0x80 };
				break;

				case SINGLE_COLOUR_RANDOM:
					data[i].red = random_triplet[0];
					data[i].green = random_triplet[1];
					data[i].blue = random_triplet[2];
				break;
			}
		}
	} else {

		status = PATTERN_STATUS_NOCHANGE;

	}

	return status;

}

/******************************************************************
 * Rainbow - show the colors of the rainbow
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *count  		Counter for flashing version
 ******************************************************************/

static uint8_t rainbow(struct RGB *data, uint8_t num_leds, uint8_t status, void *count)
{

	uint8_t *counter = (uint8_t *) count;

	if (status == PATTERN_STATUS_NEW) {

		fill_rainbow_colours(data, num_leds);
		status = PATTERN_STATUS_REFRESH;
		if (counter != NULL) { 
			*counter = 0;
		}

	} else {

		if (counter != NULL) {

			if ((*counter)++ == COLOUR_FLASH_COUNT) {
				*counter = 0;
				if (! intensity_halve(data, num_leds)) {
					fill_rainbow_colours(data, num_leds);
				}
				
				status = PATTERN_STATUS_REFRESH;
			}

		} else {
			status = PATTERN_STATUS_NOCHANGE;
		}

	}
	
	return status;

}

/******************************************************************
 * rainbow_wheel - version of rainbow that walks the leds
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *count 		Running counter 
 ******************************************************************/

static uint8_t rainbow_wheel(struct RGB *data, uint8_t num_leds, uint8_t status, void *count)
{

	uint8_t *counter = (uint8_t *) count;

	if (status == PATTERN_STATUS_NEW) {

		*counter = 0;
		fill_rainbow_colours(data, num_leds);
		status = PATTERN_STATUS_REFRESH;

	} else {

		status = PATTERN_STATUS_NOCHANGE;

		if ((*counter)++ == COLOUR_WALK_COUNT) {
			*counter = 0;
			rotate_right(data, num_leds);
			rotate_right(data, num_leds);
			rotate_right(data, num_leds);
			status = PATTERN_STATUS_REFRESH;
		}

	}
	
	return status;

}

/******************************************************************
 * fade_colours - fade & flash colours
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *fcp   		Extra params    
 ******************************************************************/

static uint8_t fade_colours(struct RGB *data, uint8_t num_leds, uint8_t status, void *fcp)
{

	struct colour_param *parameters = (struct colour_param *) fcp;
	uint8_t *counter = parameters->counter;
	uint8_t colour_type = parameters->colour_type;

	if (status == PATTERN_STATUS_NEW) {

		fill_colours(data, num_leds, colour_type);
		status = PATTERN_STATUS_REFRESH;
		*counter = 0;

	} else {

			if ((*counter)++ == COLOUR_FLASH_COUNT) {
				*counter = 0;
				if (! intensity_halve(data, num_leds)) {
					fill_colours(data, num_leds, colour_type);
				}
				
				status = PATTERN_STATUS_REFRESH;
			} else {
				status = PATTERN_STATUS_NOCHANGE;	
			}


	}
	
	return status;

}

/******************************************************************
 * crazy - flash random colours
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *count 		Running counter 
 ******************************************************************/

static uint8_t crazy(struct RGB *data, uint8_t num_leds, uint8_t status, void *count)
{

	uint8_t *counter = (uint8_t *) count;

	if (status == PATTERN_STATUS_NEW) {

		fill_single_colour(data, num_leds, status, (void *) SINGLE_COLOUR_RANDOM);
		status = PATTERN_STATUS_REFRESH;
		*counter = 0;

	} else {

			if ((*counter)++ == COLOUR_FLASH_COUNT) {
				*counter = 0;
				if (! intensity_halve(data, num_leds)) {
					fill_single_colour(data, num_leds, PATTERN_STATUS_NEW, (void *) SINGLE_COLOUR_RANDOM);
				}
				
				status = PATTERN_STATUS_REFRESH;
			} else {
				status = PATTERN_STATUS_NOCHANGE;	
			}


	}
	
	return status;

}

/******************************************************************
 * walking_colour - walk two LEDs around
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *fcp   		Extra params    
 ******************************************************************/

static uint8_t walking_colour(struct RGB *data, uint8_t num_leds, uint8_t status, void *wcp)
{

	struct colour_param *parameters = (struct colour_param *) wcp;
	uint8_t *counter = parameters->counter;
	uint8_t colour_type = parameters->colour_type;

	uint8_t triplets[4][3] = {
		{127, 0, 0},
		{0, 127, 0},
		{0, 0, 127},
		{random_byte(), random_byte(), random_byte()}
	};

	if (status == PATTERN_STATUS_NEW) {

		data[0].red = data[10].red = triplets[colour_type][0];
		data[0].green =  data[10].green =triplets[colour_type][1];
		data[0].blue =  data[10].blue =triplets[colour_type][2];

		status = PATTERN_STATUS_REFRESH;
		*counter = 0;

	} else {

			if ((*counter)++ == COLOUR_FLASH_COUNT) {
				*counter = 0;
			    rotate_right(data, num_leds);	
				status = PATTERN_STATUS_REFRESH;
			} else {
				status = PATTERN_STATUS_NOCHANGE;	
			}


	}
	
	return status;

}

/******************************************************************
 * walking_bar - walk a 4-LED bar around
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *fcp   		Extra params    
 ******************************************************************/

static uint8_t walking_bar(struct RGB *data, uint8_t num_leds, uint8_t status, void *wcp)
{

	struct colour_param *parameters = (struct colour_param *) wcp;
	uint8_t *counter = parameters->counter;
	uint8_t colour_type = parameters->colour_type;

	uint8_t triplets[4][3] = {
		{127, 0, 0},
		{0, 127, 0},
		{0, 0, 127},
		{random_byte(), random_byte(), random_byte()}
	};

	if (status == PATTERN_STATUS_NEW) {

		data[0].red = data[9].red = triplets[colour_type][0];
		data[1].red = data[10].red = triplets[colour_type][0];
		data[0].green =  data[9].green =triplets[colour_type][1];
		data[1].green =  data[10].green =triplets[colour_type][1];
		data[0].blue =  data[9].blue =triplets[colour_type][2];
		data[1].blue =  data[10].blue =triplets[colour_type][2];

		status = PATTERN_STATUS_REFRESH;
		*counter = 0;

	} else {

			if ((*counter)++ == COLOUR_FLASH_COUNT) {
				*counter = 0;
			    rotate_right(data, num_leds);	
			    rotate_right(data, num_leds);	
			    rotate_right(data, num_leds);	
				status = PATTERN_STATUS_REFRESH;
			} else {
				status = PATTERN_STATUS_NOCHANGE;	
			}


	}
	
	return status;

}

/******************************************************************
 * trilobe - divide in three parts and walk those around
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *tcp			Extra params
 ******************************************************************/

static uint8_t trilobe(struct RGB *data, uint8_t num_leds, uint8_t status, void *tcp)
{

	struct trilobe_param *parameters = (struct trilobe_param *) tcp;
	uint8_t *counter = parameters->counter;
	uint8_t *state = parameters->state;

	uint8_t bit = 0;

	// Remember that struct RGB is GRB in memory
	struct RGB orange = { 0x30, 0x80, 0x00};
	struct RGB blue = { 0x00, 0x00, 0x80};
	struct RGB red = { 0x00, 0x80, 0x00};
	struct RGB blank = { 0x00, 0x00, 0x00};

	struct fill_funcs_t {
    	void (*fill_function) (struct RGB *, uint8_t, uint8_t, struct RGB);
		uint8_t start;
		uint8_t end;
		struct RGB colour;
	};

	struct fill_funcs_t fill_funcs[3] = {
		{fill_range_colour, 12, 17, red},
		{fill_range_colour, 6, 11, blue},
		{fill_range_colour, 0, 5, orange},
	};
	
	struct fill_funcs_t ff;

	if (status == PATTERN_STATUS_NEW) {

		ff = fill_funcs[2];
		ff.fill_function(data, ff.start, ff.end, ff.colour);
		
		*counter = 0;
		*state = TRILOBE_INITIAL_STATE;

		status = PATTERN_STATUS_REFRESH;

	} else {

			if ((*counter)++ == COLOUR_WALK_COUNT) {

				*counter = 0;

				*state >>= 1;

				for (bit = 0; bit < 3; bit++) {

					ff = fill_funcs[bit];

					if (IS_BIT_SET(*state, bit)) {
						ff.fill_function(data, ff.start, ff.end, ff.colour);
					} else {
						ff.fill_function(data, ff.start, ff.end, blank);
					}

				}

				if (*state == 0) 
					*state = TRILOBE_INITIAL_STATE;
				
				status = PATTERN_STATUS_REFRESH;

			} else {

				status = PATTERN_STATUS_NOCHANGE;	

			}


	}
	
	return status;

}

/******************************************************************
 * tricircle - Circle moving outwards in three steps
 *
 * Parameter
 * 		struct RGB *  		LED data to write
 * 		uint8_t num_leds	Number of LEDs
 * 		uint8_t	status		Status of pattern
 * 		void *tccp			Extra params
 ******************************************************************/

static uint8_t tricircle(struct RGB *data, uint8_t num_leds, uint8_t status, void *tcp)
{

	struct tricircle_param *parameters = (struct tricircle_param *) tcp;
	uint8_t *counter = parameters->counter;
	uint8_t *state = parameters->state;

	uint8_t bit = 0;

	struct RGB colour = get_colour_from_parameter(parameters->colour_type);
	struct RGB blank = { 0x00, 0x00, 0x00};

	struct fill_funcs_t {
    	void (*fill_function) (struct RGB *, uint8_t , uint8_t *, struct RGB);
		uint8_t num_leds;
		uint8_t *leds;
		struct RGB colour;
	};

	uint8_t inner_circle[6] = {2, 5, 8, 11, 14, 17};
	uint8_t middle_circle[6] = {1, 3, 6, 9, 12, 15};
	uint8_t outer_circle[6] = {0, 4, 7, 10, 13, 16};

	struct fill_funcs_t fill_funcs[3] = {
		{fill_some_colour, 6, outer_circle, colour},
		{fill_some_colour, 6, middle_circle, colour},
		{fill_some_colour, 6, inner_circle, colour},
	};
	
	struct fill_funcs_t ff;

	if (status == PATTERN_STATUS_NEW) {

		ff = fill_funcs[2];
		ff.fill_function(data, ff.num_leds, ff.leds, ff.colour);
		
		*counter = 0;
		*state = TRICIRCLE_INITIAL_STATE;

		status = PATTERN_STATUS_REFRESH;

	} else {

			if ((*counter)++ == COLOUR_WALK_COUNT) {

				*counter = 0;

				*state >>= 1;

				for (bit = 0; bit < 3; bit++) {

					ff = fill_funcs[bit];

					if (IS_BIT_SET(*state, bit)) {
						ff.fill_function(data, ff.num_leds, ff.leds, ff.colour);
					} else {
						ff.fill_function(data, ff.num_leds, ff.leds, blank);
					}

				}

				if (*state == 0) 
					*state = TRICIRCLE_INITIAL_STATE;
				
				status = PATTERN_STATUS_REFRESH;

			} else {

				status = PATTERN_STATUS_NOCHANGE;	

			}


	}
	
	return status;

}


/******************************************************************
 * init_IO: initialise I/O pins
 ******************************************************************/

static void init_IO(void)
{

	DDRB = 0b00000001;	// All input except PB0
    PORTB = 0b11111110; // All pullup, PB0 low

}

/******************************************************************
 * init_debounce_timer: initialise Timer0 for debouncing
 *
 * Timer0 runs in CTC mode with a /1024 prescaler and counts to 200
 * This results in ISR every 10ms, which will be used to debounce
 ******************************************************************/

static void init_debounce_timer(void) 
{

	// CTC mode
	TCCR0A &= ~(1 << WGM00);
	TCCR0A |= (1 << WGM01);
	TCCR0B &= ~(1 << WGM02);

	// Count to 200
	OCR0A = 200;

	// Enable Compare Match interrupt
	TIMSK |= (1 << OCIE0A);

	// Start timer with prescaler /1024
	TCCR0B |= (1 << CS02 | 1 << CS00);
	TCCR0B &= ~(1 << CS01);

}

/******************************************************************
 * Timer0 compare match interrupt: debounce button press
 ******************************************************************/

volatile uint8_t short_press = 0;
volatile uint8_t isr_short_press = 0;
volatile uint8_t long_press = 0;
volatile uint8_t next_pattern = 0;
volatile uint8_t demo_mode = 0;
volatile uint8_t current_debounce_count = 0;
volatile uint8_t button_press_acknowledged = 1;
volatile uint16_t demo_time_counter = 0;

ISR(TIM0_COMPA_vect)
{

	if (button_press_acknowledged) {

		switch (current_debounce_count) {

			case 0:
				if (bit_is_clear(PINB,BUTTON)) {
					current_debounce_count = 1;
				}
				break;

			case DEBOUNCE_COUNT_SHORT:
				if (bit_is_clear(PINB,BUTTON)) {
					// Press detected
					isr_short_press = 1;
				}
				current_debounce_count++;
				break;

			case DEBOUNCE_COUNT_MID:
				if (isr_short_press && bit_is_set(PINB,BUTTON)) {
					// Button no longer pressed: it's a short
					short_press = 1;
					button_press_acknowledged = 0;
					current_debounce_count = 0;	
					isr_short_press = 0;
				} else {
					// Button still pressed - wait for a long press
					current_debounce_count++;
				}
				break;

			// If we get to this case, the button has been pressed and was still
			// pressed at DEBOUNCE_COUNT_MID. So it is (probably) not two short
			// presses DEBOUNCE_COUNT_LONG apart
			case DEBOUNCE_COUNT_LONG:
				if (bit_is_clear(PINB,BUTTON)) {
					// It's a long press
					long_press = 1;
					button_press_acknowledged = 0;
				} else if (isr_short_press) {
					// It was a short press after all
					short_press = 1;
					button_press_acknowledged = 0;
				}
				current_debounce_count = 0;
				isr_short_press = 0;
				break;

			default:	// 1 to DEBOUNCE_COUNT_LONG-1
				current_debounce_count++;
				break;
			
				
		}
	}

	if (demo_mode) {
	
		if(++demo_time_counter == DEMO_TIME_COUNT) {
			demo_time_counter = 0;
			next_pattern = 1;
		}

	}

}

/******************************************************************
 * copy_buffer: copy struct RGB
 * 
 * Parameters:
 *
 * 		struct RGB *source
 * 		struct RGB *dest
 *		uint8_t	size
 ******************************************************************/

static void copy_buffer(struct RGB *source, struct RGB *dest, uint8_t size)
{

	uint16_t i; 

	for (i = 0 ; i < size*3 ; i++) {
		*dest++ = *source++;
	}

}

/******************************************************************
 * quick_flash_leds: quickly flash all leds 
 * 
 * Parameters:
 *
 *		struct RGB *data	
 *		static uint8_t num_leds
 ******************************************************************/

static void quick_flash_leds(struct RGB* data, uint8_t num_leds) 
{

	uint8_t flash_count = 0;

	struct RGB *buffer = malloc(sizeof(struct RGB) * NUM_LEDS);

	copy_buffer(data, buffer, NUM_LEDS);

	for (flash_count = 0; flash_count < NUM_QUICK_FLASH; flash_count++) {
		while(intensity_halve(buffer, num_leds)) {
			send_frame(buffer, num_leds, LED_PIN);
			_delay_ms(QUICK_FLASH_DELAY);
		}

		copy_buffer(data, buffer, NUM_LEDS);
		send_frame(buffer, num_leds, LED_PIN);
		_delay_ms(QUICK_FLASH_DELAY);
	}

	// Restore original pattern
	send_frame(data, num_leds, LED_PIN);

	// Don't leave memory hanging around
	free(buffer);

}

/******************************************************************
 * Main 
 ******************************************************************/

int main(void)
{

	uint8_t pattern_status = PATTERN_STATUS_NEW;
	uint8_t current_pattern = 0;
	uint8_t fading = 0;
	
	struct patternfunc fade_down_pf = { fade_down, (void *) &pattern_counter };
	
	struct RGB *led_data = malloc(sizeof(struct RGB) * NUM_LEDS);

    // pattern_status = patternfunc(data, NUM_LEDS, pattern_status, param);	
    // on next pattern, reset pattern_status to 0

	init_IO();
	init_debounce_timer();
	sei();

	srand(42);

	while (1) {

		struct patternfunc pf;

		// Fetch pattern function
		if (! fading ) {
			pf = pattern_functions[current_pattern];
		} else {
			pf = fade_down_pf;
		}

		// Run pattern function
		pattern_status = pf.run_pattern(led_data, NUM_LEDS, pattern_status, pf.extra_parameter);

		// Check status
		switch (pattern_status) {

			case PATTERN_STATUS_REFRESH:

				send_frame(led_data, NUM_LEDS, LED_PIN);
				break;

			case PATTERN_STATUS_FADE_DONE:

				fading = 0;
				pattern_status = PATTERN_STATUS_NEW;
				break;

		}

		// Check for short button press: next pattern
		if (!demo_mode && short_press) {
			short_press = 0;
			button_press_acknowledged = 1;
			next_pattern = 1;
		}

		// Check for long button press: demo mode
		if (long_press) {
			long_press = 0;
			demo_time_counter = 0;
			quick_flash_leds(led_data, NUM_LEDS); // Acknowledge the press
			demo_mode ^= 0x01;
			button_press_acknowledged = 1;
		}

		// Check for next pattern: can come from button press or ISR in demo mode
		if (next_pattern) {
			next_pattern = 0;
			pattern_status = PATTERN_STATUS_NEW;
			if (++current_pattern == NUM_PATTERNS) {current_pattern = 0;}
			fading = 1;
		}
	
		_delay_ms(FRAME_DELAY);


	}

	return 0;
}
