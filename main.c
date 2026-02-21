/****************************************************************************
* FILE:      main.c															*
* CONTENTS:  Mainline														*
* COPYRIGHT: MadLab Ltd. 2025												*
* AUTHOR:    James Hutchby													*
* UPDATED:   09/01/25														*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "common.h"
#include "main.h"
#include "enigma.h"
#include "keyboard.h"
#include "usb_hid_keys.h"
#include "lcd.h"


//---------------------------------------------------------------------------
// notes
//---------------------------------------------------------------------------

// on power-up PICO LED flashes twice and displays splash screen for a couple of seconds
// then displays logo/credits screen

// tests for USB keyboard presence and displays error message if not connected
// otherwise waits for key press then displays main menu
// LED flashes twice when keyboard connected

// F1 displays menu
// F2 sets rotors
//  Resets ring and initial position to 'A' when rotor changed.
//  Note that the same rotor can't be used more than once.
// F3 sets rings
// F4 sets reflector
// F5 sets plug board
//  Note a plug can't connect to itself and a letter can't be used more than once.
// F6 sets initial rotor positions
//  Always resets rotors.
// F7 displays current rotor positions (read only)
// F8 saves current settings
// F9 loads saved settings

// Rotors reset if any changes made.

// letters 'A' to 'Z' encoded and displayed
// rotors stepped before each letter encoded

// ESC/HOME resets rotors to initial positions (i.e. clears message)
// display cleared to acknowledge

// BACKSPACE/DELETE deletes last letter entered (steps rotors back one position)
// single level of undo only


//---------------------------------------------------------------------------
// port assignments
//---------------------------------------------------------------------------

#define LED_PIN PICO_DEFAULT_LED_PIN


//---------------------------------------------------------------------------
// constants
//---------------------------------------------------------------------------

#define DEBUG false			// true if debugging


//---------------------------------------------------------------------------
// linkage
//---------------------------------------------------------------------------

extern bool KeyboardDetected;
extern uint16_t enigma[], logo[];
extern uint16_t A[], B[], C[], D[], E[], F[], G[], H[];
extern uint16_t I[], J[], K[], L[], M[], N[], O[], P[];
extern uint16_t Q[], R[], S[], T[], U[], V[], W[], X[];
extern uint16_t Y[], Z[];


//---------------------------------------------------------------------------
// variables
//---------------------------------------------------------------------------

static int encoded_letter, last_letter;


//---------------------------------------------------------------------------
// main entry point
//---------------------------------------------------------------------------

int main()
{
	// stdio_init_all();

	// initialise PICO LED
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);

	// double flash LED
	FlashLED();
	FlashLED();

	// initialise LCD display
	InitDisplay();

	// display splash screen
	DrawImage(enigma, WIDTH, HEIGHT);

	sleep_ms(2000);

	// initialise USB keyboard
	InitKeyboard();

	// wait until keyboard connected
	int timeout = 300;
	while (true)
	{
		ScanKeyboard();
		if (KeyboardDetected) break;
		sleep_ms(10);
		if (timeout > 0 && --timeout == 0)
		{
			ClearDisplay(BLACK);
			DrawDoubleString(24, 100, "  Keyboard  ", RED, BLACK);
			DrawDoubleString(24, 120, "not detected", RED, BLACK);
		}
	}

	// display logo etc.
	DrawImage(logo, WIDTH, HEIGHT);
	DrawDoubleString(32, 150, "version 2.0", BLUE, WHITE);
	DrawDoubleString(8, 170, "www.madlab.org", BLUE, WHITE);
	DrawDoubleString(32, 190, "@clubmadlab", BLUE, WHITE);
	DrawDoubleString(0, 210, "(c) MadLab 2025", BLUE, WHITE);

	// wait for key press
	while (true)
	{
		ScanKeyboard();
		if (GetKeyCode() != -1) break;
	}

	// initialise Enigma
	InitialiseRotors();
	InitialiseReflector();
	InitialisePlugBoard();

	watchdog_enable(2000, 1);


//---------------------------------------------------------------------------
// main loop
//---------------------------------------------------------------------------

	encoded_letter = last_letter = -1;

	int key = -1;

	while (true)
	{
		display_menu();

		while (true)
		{
			watchdog_update();

			if (key == -1)
			{
				ScanKeyboard();
				key = GetKeyCode();
				if (key == -1) continue;
			}

			key = process_key(key);
			if (key == -1) continue; else break;
		}
	}
}


static uint16_t* letters[26] =
{A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z};

// processes a key
int process_key(int key)
{
	if (key >= KEY_A && key <= KEY_Z)
	{
		// encode and display letter
		last_letter = encoded_letter;
		encoded_letter = EncodeLetter('A' + (key - KEY_A));
		DrawImage(letters[encoded_letter - 'A'], WIDTH/2, HEIGHT/2);
		return -1;
	}

	switch (key)
	{
	case KEY_F1:
		display_menu();
		return -1;
	case KEY_F2:
		return SetRotors();
	case KEY_F3:
		return SetRings();
	case KEY_F4:
		return SetReflector();
	case KEY_F5:
		return SetPlugBoard();
	case KEY_F6:
		return SetInitialPositions();
	case KEY_F7:
		return ShowCurrentPositions();
	case KEY_F8:
		ResetRotors();
		SaveSettings();
		DrawDoubleString(0, 220, "               ", WHITE, BLACK);
		DrawDoubleString(8, 220, "Settings saved", WHITE, BLACK);
  		return -1;
	case KEY_F9:
		LoadSettings();
		ResetRotors();
		encoded_letter = last_letter = -1;
		DrawDoubleString(0, 220, "               ", WHITE, BLACK);
		DrawDoubleString(0, 220, "Settings loaded", WHITE, BLACK);
		return -1;

	case KEY_ESC:
	case KEY_HOME:
		ResetRotors();
		ClearDisplay(BLACK);
		encoded_letter = last_letter = -1;
		return -1;

	case KEY_BACKSPACE:
	case KEY_DELETE:
		UndoStep();
		// re-display previous letter
		if (last_letter == -1) ClearDisplay(BLACK);
		else DrawImage(letters[last_letter - 'A'], WIDTH/2, HEIGHT/2);
		return -1;

	default:
		return -1;
	}
}


// displays the main menu
void display_menu()
{
	ClearDisplay(BLACK);

	DrawTripleString(48, 0, "Enigma", HEADING_COLOUR, BLACK);

	DrawDoubleString(8, 48, "F1 = Menu", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 66, "F2 = Rotors", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 84, "F3 = Rings", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 102, "F4 = Reflector", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 120, "F5 = Plugs", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 138, "F6 = Initial", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 156, "F7 = Current", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 174, "F8 = Save", MENU_COLOUR, BLACK);
	DrawDoubleString(8, 192, "F9 = Load", MENU_COLOUR, BLACK);
}


// flashes the PICO LED
void FlashLED()
{
	gpio_put(LED_PIN, 1);
	sleep_ms(250);
	gpio_put(LED_PIN, 0);
	sleep_ms(250);
	watchdog_update();
}
