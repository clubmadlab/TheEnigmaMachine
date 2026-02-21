/****************************************************************************
* FILE:      lcd.c															*
* CONTENTS:  LCD display													*
* COPYRIGHT: MadLab Ltd. 2025												*
* AUTHOR:    James Hutchby													*
* UPDATED:   09/01/25														*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"

#include "common.h"
#include "lcd.h"
#include "lcd.pio.h"


//---------------------------------------------------------------------------
// port assignments
//---------------------------------------------------------------------------

#define SCL_PIN 18			// serial clock (active low)
#define SDA_PIN 19			// serial data
#define DC_PIN 27			// data/command (high = data, low = command)
#define RES_PIN 26			// reset (active low)
#define BLK_PIN 28			// backlight


//---------------------------------------------------------------------------
// constants
//---------------------------------------------------------------------------

#define SPI_SPEED 48000000L		// SPI speed in Hz


//---------------------------------------------------------------------------
// ST7789 commands
//---------------------------------------------------------------------------

#define SWRESET 0x01			// software reset
#define RDDID 0x04				// read display ID
#define RDDST 0x09				// read display status
#define RDDPM 0x0a				// read display power mode
#define RDDMADCTL 0x0b			// read display MADCTL
#define RDDCOLMOD 0x0c			// read display pixel format
#define RDDIM 0x0d				// read display image mode
#define RDDSM 0x0e				// read display signal mode
#define RDDSDR 0x0f				// read display self-diagnostic result
#define SLPIN 0x10				// sleep in
#define SLPOUT 0x11				// sleep out
#define PTLON 0x12				// partial display mode on
#define NORON 0x13				// normal display mode on
#define INVOFF 0x20				// display inversion off
#define INVON 0x21				// display inversion on
#define GAMSET 0x26				// gamma set
#define DISPOFF 0x28			// display off
#define DISPON 0x29				// display on
#define CASET 0x2a				// column address set
#define RASET 0x2b				// row address set
#define RAMWR 0x2c				// memory write
#define RAMRD 0x2e				// memory read
#define PTLAR 0x30				// partial area
#define VSCRDEF 0x33			// vertical scrolling definition
#define TEOFF 0x34				// tearing effect line off
#define TEON 0x35				// tearing effect line on
#define MADCTL 0x36				// memory data access control
#define VSCSAD 0x37				// vertical scroll start address of RAM
#define IDMOFF 0x38				// idle mode off
#define IDMON 0x39				// idle mode on
#define COLMOD 0x3a				// interface pixel format
#define WRMEMC 0x3c				// write memory continue
#define RDMEMC 0x3e				// read memory continue
#define STE 0x44				// set tear scanline
#define GSCAN 0x45				// get scanline
#define WRDISBV 0x51			// write display brightness
#define RDDISBV 0x52			// read display brightness
#define WRCTRLD 0x53			// write CTRL display
#define RDCTRLD 0x54			// read CTRL display
#define WRCACE 0x55				// write content adaptive brightness control and colour enhancement
#define RDCACE 0x56				// read content adaptive brightness control
#define WRCABCMB 0x5e			// write CABC minimum brightness
#define RDCABCMB 0x5f			// read CABC minimum brightness
#define RDABCSDR 0x68			// read automatic brightness control self-diagnostic result
#define RDID1 0xda				// read ID1
#define RDID2 0xdb				// read ID2
#define RDID3 0xdc				// read ID3

#define MADCTL_MY (1<<7)		// page address order
#define MADCTL_MX (1<<6)		// column address order
#define MADCTL_MV (1<<5)		// page/column order
#define MADCTL_ML (1<<4)		// line address order
#define MADCTL_RGB (0<<3)		// RGB order
#define MADCTL_MH (1<<2)		// display data latch order


//---------------------------------------------------------------------------
// variables
//---------------------------------------------------------------------------

static PIO pio;						// PIO instance
static uint sm;						// state machine instance


//---------------------------------------------------------------------------
// linkage
//---------------------------------------------------------------------------

extern uint8_t Font[];


//---------------------------------------------------------------------------
// functions
//---------------------------------------------------------------------------

// transmits a command byte to the display
static inline void tx_command(uint8_t b)
{
	// command
	pio_sm_put_blocking(pio, sm, (0 << 31) | (b << 23));
}

// transmits a data byte to the display
static inline void tx_data(uint8_t b)
{
	// data
	pio_sm_put_blocking(pio, sm, (1 << 31) | (b << 23));
}

// transmits a data word to the display
static inline void tx_word(uint16_t w)
{
	// data
	pio_sm_put_blocking(pio, sm, (1 << 31) | (w << 15));
	pio_sm_put_blocking(pio, sm, (1 << 31) | (w << 23));
}


// initialises the display
void InitDisplay()
{
	// configure ports
	gpio_init(RES_PIN);
	gpio_set_dir(RES_PIN, GPIO_OUT);
	gpio_put(RES_PIN, 1);

	// backlight on
	gpio_init(BLK_PIN);
	gpio_set_dir(BLK_PIN, GPIO_OUT);
	gpio_put(BLK_PIN, 1);

	pio = pio1;

	// 2 system clocks per SPI clock
	uint32_t clock_mhz = clock_get_hz(clk_sys) / 1000000L;
	uint32_t spi_mhz = (SPI_SPEED * 2) / 1000000L;

	uint16_t div = (uint16_t) (clock_mhz / spi_mhz);
	uint32_t rem = clock_mhz - spi_mhz * div;
	uint8_t frac = (uint8_t) ((rem * 256L) / spi_mhz);

	sm = 0;
	uint offset = pio_add_program(pio, &lcd_program);
	lcd_program_init(pio, sm, offset, SDA_PIN, SCL_PIN, DC_PIN, div, frac);

	// reset display
	gpio_put(RES_PIN, 1);
	sleep_ms(10);
	gpio_put(RES_PIN, 0);
	sleep_ms(10);
	gpio_put(RES_PIN, 1);
	sleep_ms(10);

	// software reset
	tx_command(SWRESET);
	sleep_ms(150);

	// exit sleep mode
	tx_command(SLPOUT);
	sleep_ms(500);

	// set colour mode
	tx_command(COLMOD);
	// 16-bit colour
	tx_data(0x55);
	sleep_ms(10);

	// set memory data access control
	tx_command(MADCTL);
	// left to right, top to bottom, RGB
	tx_data(0x00);
	sleep_ms(10);

//	tx_command(WRCACE);
//	tx_data(0x00);
//	sleep_ms(10);

//	tx_command(GAMSET);
//	tx_data(0x01);
//	sleep_ms(10);

//	tx_command(TEON);
//	tx_data(0x00);
//	sleep_ms(10);

//	tx_command(WRCTRLD);
//	tx_data(0x00);
//	sleep_ms(10);

//	tx_command(WRDISBV);
//	tx_data(0x00);
//	sleep_ms(10);

	ClearDisplay(BLACK);

	// display inversion on
	tx_command(INVON);
	sleep_ms(10);

	// normal display on
	tx_command(NORON);
	sleep_ms(10);

	// screen on
	tx_command(DISPON);
	sleep_ms(500);
}


// turns the display on
void DisplayOn()
{
	// backlight on
	gpio_put(BLK_PIN, 1);

	// screen on
	tx_command(DISPON);
}

// turns the display off
void DisplayOff()
{
	// backlight off
	gpio_put(BLK_PIN, 0);

	ClearDisplay(BLACK);

	// screen off
	tx_command(DISPOFF);
}

// clears the display
void ClearDisplay(uint colour)
{
	// set column address
	tx_command(CASET);
	tx_word(0);
	tx_word(WIDTH - 1);

	// set row address
	tx_command(RASET);
	tx_word(0);
	tx_word(HEIGHT - 1);

	// write RAM
	tx_command(RAMWR);

	// clear all pixels
	for (unsigned long n = WIDTH * HEIGHT; n > 0; n--) tx_word(colour);
}


// draws a string on the display, returns updated x-coord
int DrawString(int x, int y, const char* s, uint foreground, uint background)
{
	while (*s != 0) x = DrawChar(x, y, *s++, foreground, background);

	return x;
}

// draws a double-size string on the display, returns updated x-coord
int DrawDoubleString(int x, int y, const char* s, uint foreground, uint background)
{
	while (*s != 0) x = DrawDoubleChar(x, y, *s++, foreground, background);

	return x;
}

// draws a triple-size string on the display, returns updated x-coord
int DrawTripleString(int x, int y, const char* s, uint foreground, uint background)
{
	while (*s != 0) x = DrawTripleChar(x, y, *s++, foreground, background);

	return x;
}

// draws a number of characters on the display, returns updated x-coord
int DrawChars(int x, int y, const char* s, int len, uint foreground, uint background)
{
	while (len-- > 0) x = DrawChar(x, y, *s++, foreground, background);

	return x;
}

// draws a character on the display, returns updated x-coord
int DrawChar(int x, int y, char c, uint foreground, uint background)
{
	// set column address
	tx_command(CASET);
	tx_word(x);
	tx_word(x + 7);

	// set row address
	tx_command(RASET);
	tx_word(y);
	tx_word(y + 7);

	// write RAM
	tx_command(RAMWR);

	if (c == ' ')
	{
		// special case
		for (int n = 8*8; n > 0; n--) tx_word(background);
	}
	else
	{
		uint8_t* p = Font + ((c - ' ') << 3);

		for (int n = 8; n > 0; n--)
		{
			uint8_t dots = *p++;
			tx_word(dots & (1<<7) ? foreground : background);
			tx_word(dots & (1<<6) ? foreground : background);
			tx_word(dots & (1<<5) ? foreground : background);
			tx_word(dots & (1<<4) ? foreground : background);
			tx_word(dots & (1<<3) ? foreground : background);
			tx_word(dots & (1<<2) ? foreground : background);
			tx_word(dots & (1<<1) ? foreground : background);
			tx_word(dots & (1<<0) ? foreground : background);
		}
	}

	return x + 8;
}

// draws a double-size character on the display, returns updated x-coord
int DrawDoubleChar(int x, int y, char c, uint foreground, uint background)
{
	// set column address
	tx_command(CASET);
	tx_word(x);
	tx_word(x + 15);

	// set row address
	tx_command(RASET);
	tx_word(y);
	tx_word(y + 15);

	// write RAM
	tx_command(RAMWR);

	if (c == ' ')
	{
		// special case
		for (int n = 16*16; n > 0; n--) tx_word(background);
	}
	else
	{
		uint8_t* p = Font + ((c - ' ') << 3);

		for (int n = 16; n > 0; n--)
		{
			uint colour;
			uint8_t dots = *p;
			colour = dots & (1<<7) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<6) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<5) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<4) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<3) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<2) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<1) ? foreground : background; tx_word(colour); tx_word(colour);
			colour = dots & (1<<0) ? foreground : background; tx_word(colour); tx_word(colour);
			if (n & 1) p++;
		}
	}

	return x + 16;
}

// draws a triple-size character on the display, returns updated x-coord
int DrawTripleChar(int x, int y, char c, uint foreground, uint background)
{
	// set column address
	tx_command(CASET);
	tx_word(x);
	tx_word(x + 23);

	// set row address
	tx_command(RASET);
	tx_word(y);
	tx_word(y + 23);

	// write RAM
	tx_command(RAMWR);

	if (c == ' ')
	{
		// special case
		for (int n = 24*24; n > 0; n--) tx_word(background);
	}
	else
	{
		uint8_t* p = Font + ((c - ' ') << 3);

		for (int n = 24; n > 0; n--)
		{
			uint colour;
			uint8_t dots = *p;
			colour = dots & (1<<7) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<6) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<5) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<4) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<3) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<2) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<1) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			colour = dots & (1<<0) ? foreground : background; tx_word(colour); tx_word(colour); tx_word(colour);
			if ((n % 3) == 1) p++;
		}
	}

	return x + 24;
}


// displays the complete font
void DrawFont()
{
	ClearDisplay(BLACK);

	DrawDoubleString(0, 0, "!\"#$%&'()*+,-./", BLUE, BLACK);
	DrawDoubleString(0, 16, "0123456789", RED, BLACK);
	DrawDoubleString(0, 32, ":;<=>?@", GREEN, BLACK);
	DrawDoubleString(0, 48, "ABCDEFGHIJKLM", CYAN, BLACK);
	DrawDoubleString(0, 64, "NOPQRSTUVWXYZ", MAGENTA, BLACK);
	DrawDoubleString(0, 80, "[\\]^_`", YELLOW, BLACK);
	DrawDoubleString(0, 96, "abcdefghijklm", WHITE, BLACK);
	DrawDoubleString(0, 112, "nopqrstuvwxyz", ORANGE, BLACK);
	DrawDoubleString(0, 128, "{|}~", BLUE, BLACK);
}


// draws a bitmap image on the display
void DrawImage(uint16_t* pixels, int width, int height)
{
	uint16_t* p1 = pixels + width * (height-1);

	// set column address
	tx_command(CASET);
	tx_word(0);
	tx_word(WIDTH - 1);

	// set row address
	tx_command(RASET);
	tx_word(0);
	tx_word(HEIGHT - 1);

	// write RAM
	tx_command(RAMWR);

	if (width == WIDTH/2 && height == HEIGHT/2)
	{
		// double pixels horizontally and vertically
		uint16_t* p2 = p1;

		for (int y = 0; y < height; y++)
		{
			uint16_t w;
			for (int x = width/4; x > 0; x--)
			{
				w = *p1++;
				tx_word(w);
				tx_word(w);
				w = *p1++;
				tx_word(w);
				tx_word(w);
				w = *p1++;
				tx_word(w);
				tx_word(w);
				w = *p1++;
				tx_word(w);
				tx_word(w);
			}
			for (int x = width/4; x > 0; x--)
			{
				w = *p2++;
				tx_word(w);
				tx_word(w);
				w = *p2++;
				tx_word(w);
				tx_word(w);
				w = *p2++;
				tx_word(w);
				tx_word(w);
				w = *p2++;
				tx_word(w);
				tx_word(w);
			}

			p1 -= width * 2;
			p2 -= width * 2;
		}
	}
	else
	{
		for (int y = 0; y < height; y++)
		{
			uint16_t w;
			for (int x = width/8; x > 0; x--)
			{
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
				w = *p1++;
				tx_word(w);
			}

			p1 -= width * 2;
		}
	}
}
