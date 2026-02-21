/****************************************************************************
* FILE:      enigma.c														*
* CONTENTS:  Enigma set-up and encoding/decoding							*
* COPYRIGHT: MadLab Ltd. 2025												*
* AUTHOR:    James Hutchby													*
* UPDATED:   29/01/25														*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"
#include "hardware/flash.h"
#include "hardware/claim.h"

#include "common.h"
#include "enigma.h"
#include "lcd.h"
#include "keyboard.h"
#include "usb_hid_keys.h"


//---------------------------------------------------------------------------
// specification
//---------------------------------------------------------------------------

// M3 Naval Enigma - machine setup characterised by:
// left, centre and right rotor numbers (0 to 7 = 1/I to 8/VIII)
// left, centre and right rotor rings (0 to 25 = A to Z)
// left, centre and right rotor initial positions (0 to 25 = A to Z)
// reflector (0 or 1 = B or C)
// plug board - up to 13 wires connecting pairs of letters (0 to 25 = A to Z)

// default settings - rotors 1, 2, 3; rings A, A, A; initial A, A, A; reflector B; no plugs


//---------------------------------------------------------------------------
// structures and variables
//---------------------------------------------------------------------------

struct Rotor
{
	int number;			// rotor number (0 to 7 = 1/I to 8/VIII)
	int ring;			// rotor ring (0 to 25 = A to Z)
	int initial;		// initial position (0 to 25 = A to Z)
	int position;		// current position (0 to 25 = A to Z)
	int saved;			// saved position
};

static struct Rotor LeftRotor;			// left rotor
static struct Rotor CentreRotor;		// centre rotor
static struct Rotor RightRotor;			// right rotor
static int Reflector;					// reflector (0 or 1 = B or C)
static int PlugBoard[13][2];			// plug board pairs (0 to 25 = A to Z)


//---------------------------------------------------------------------------
// encoding/decoding
//---------------------------------------------------------------------------

static char* RotorTable[] =
{
	"EKMFLGDQVZNTOWYHXUSPAIBRCJ",		// rotor 1/I
	"AJDKSIRUXBLHWTMCQGZNPYFVOE",		// rotor 2/II
	"BDFHJLCPRTXVZNYEIWGAKMUSQO",		// rotor 3/III
	"ESOVPZJAYQUIRHXLNFTGKDCMWB",		// rotor 4/IV
	"VZBRGITYUPSDNHLXAWMJQOFECK",		// rotor 5/V
	"JPGVOUMFYQBENHZRDKASXLICTW",		// rotor 6/VI
	"NZJHGRCXMYSWBOUFAIVLPEKQDT",		// rotor 7/VII
	"FKQHTLXOCBJSPDZRAMEWNIUYGV",		// rotor 8/VIII
	#if false
	"LEYJVCNIXWPBQMDRTAKZGFUHOS",		// beta rotor - 'thin' 4th rotor used in leftmost position
	"FSOKANUERHMBTIYCWLQPZXVGJD",		// gamma rotor - 'thin' 4th rotor used in leftmost position
	#endif
};

static char* NotchTable[] =
{
	"QQ",								// rotor 1/I
	"EE",								// rotor 2/II
	"VV",								// rotor 3/III
	"JJ",								// rotor 4/IV
	"ZZ",								// rotor 5/V
	"ZM",								// rotor 6/VI
	"ZM",								// rotor 7/VII
	"ZM",								// rotor 8/VIII
	#if false
	"..",								// beta rotor - no notch as only used in 4th position
	"..",								// gamma rotor - no notch as only used in 4th position
	#endif
};

static char* ReflectorTable[] =
{
	"YRUHQSLDPXNGOKMIEBFZCWVJAT",		// reflector B
	"FVPJIAOYEDRZXWGCTKUQSBNMHL",		// reflector C
	#if false
	"ENKQAUYWJICOPBLMDXZVFTHRGS",		// 'thin' reflector B
	"RDOBJNTKVEHMLFCWZAXGYIPSUQ",		// 'thin' reflector C
	#endif
};


// steps the rotors forwards one position
static void step_rotors()
{
	// save current position
	LeftRotor.saved = LeftRotor.position;
	CentreRotor.saved = CentreRotor.position;
	RightRotor.saved = RightRotor.position;

	#define notch(rotor) (rotor.position == NotchTable[rotor.number][0] - 'A' || rotor.position == NotchTable[rotor.number][1] - 'A')
	#define rotate(rotor) rotor.position = (rotor.position + 1) % 26;

	// centre rotor at notch ?
	if (notch(CentreRotor))
	{
		// rotate left rotor
		rotate(LeftRotor);

		// rotate centre rotor
		rotate(CentreRotor);

		// rotate right rotor
		rotate(RightRotor);

		return;
	}

	// right rotor at notch ?
	if (notch(RightRotor))
	{
		// rotate centre rotor
		rotate(CentreRotor);
	}

	// rotate right rotor
	rotate(RightRotor);
}


// transposes a letter at the plug board
static void transpose_plug_board(char* p)
{
	for (int i = 0; i < 13; i++)
	{
		if (PlugBoard[i][0] == *p - 'A' && PlugBoard[i][1] != -1)
		{
			*p = 'A' + PlugBoard[i][1];
			return;
		}
		else if (PlugBoard[i][1] == *p - 'A' && PlugBoard[i][0] != -1)
		{
			*p = 'A' + PlugBoard[i][0];
			return;
		}
	}
}


// helper function
static inline void adjust(char* p, int offset)
{
	int n = (*p - 'A') + offset;
	if (n < 0) n += 26; else if (n >= 26) n -= 26;
	*p = 'A' + n;
}


// transposes a letter through a rotor in the forward (right-to-left) direction
static void transpose_rotor_forward(struct Rotor rotor, char* p)
{
	// adjust for ring position
	adjust(p, -rotor.ring);

	// adjust for rotor position
	adjust(p, +rotor.position);

	// transpose letter
	*p = RotorTable[rotor.number][*p - 'A'];

	// adjust for rotor position
	adjust(p, -rotor.position);

	// adjust for ring position
	adjust(p, +rotor.ring);
}


// transposes a letter through a rotor in the reverse (left-to-right) direction
static void transpose_rotor_reverse(struct Rotor rotor, char* p)
{
	// adjust for ring position
	adjust(p, -rotor.ring);

	// adjust for rotor position
	adjust(p, +rotor.position);

	// transpose letter
	int i = 0;
	while (i < 26 && *p != RotorTable[rotor.number][i]) i++;
	*p = 'A' + i;

	// adjust for rotor position
	adjust(p, -rotor.position);

	// adjust for ring position
	adjust(p, +rotor.ring);
}


// transposes a letter at the reflector
static void transpose_reflector(char* p)
{
	*p = ReflectorTable[Reflector][*p - 'A'];
}


// encodes a letter
char EncodeLetter(char letter)
{
	// rotors step before encoding
	step_rotors();

	// plug board
	transpose_plug_board(&letter);

	// rotors forward
	transpose_rotor_forward(RightRotor, &letter);
	transpose_rotor_forward(CentreRotor, &letter);
	transpose_rotor_forward(LeftRotor, &letter);

	// reflector
	transpose_reflector(&letter);

	// rotors reverse
	transpose_rotor_reverse(LeftRotor, &letter);
	transpose_rotor_reverse(CentreRotor, &letter);
	transpose_rotor_reverse(RightRotor, &letter);

	// plug board
	transpose_plug_board(&letter);

	return letter;
}


// steps the rotors back to the saved position
void UndoStep()
{
	// restore saved position
	LeftRotor.position = LeftRotor.saved;
	CentreRotor.position = CentreRotor.saved;
	RightRotor.position = RightRotor.saved;
}


//---------------------------------------------------------------------------
// settings
//---------------------------------------------------------------------------

// 0, 1, 2 = left, centre, right
static int current_rotor;

// true if changes made to settings
static bool changed;

// initialises the rotors
void InitialiseRotors()
{
	// default left rotor 1/I
	LeftRotor.number = 0;
	LeftRotor.ring = 0;
	LeftRotor.saved = LeftRotor.position = LeftRotor.initial = 0;

	// default centre rotor 2/II
	CentreRotor.number = 1;
	CentreRotor.ring = 0;
	CentreRotor.saved = CentreRotor.position = CentreRotor.initial = 0;

	// default right rotor 3/III
	RightRotor.number = 2;
	RightRotor.ring = 0;
	RightRotor.saved = RightRotor.position = RightRotor.initial = 0;
}

// initialises the reflector
void InitialiseReflector()
{
	// default reflector B
	Reflector = 0;
}

// initialises the plug board
void InitialisePlugBoard()
{
	for (int i = 0; i < 13; i++) PlugBoard[i][0] = PlugBoard[i][1] = -1;
}


// resets the rotors
void ResetRotors()
{
	// reset rotor positions
	LeftRotor.saved = LeftRotor.position = LeftRotor.initial;
	CentreRotor.saved = CentreRotor.position = CentreRotor.initial;
	RightRotor.saved = RightRotor.position = RightRotor.initial;
}


//---------------------------------------------------------------------------
// setting the rotors
//---------------------------------------------------------------------------

static void show_rotors(bool clear)
{
	if (clear) ClearDisplay(BLACK);

	DrawTripleString(60, 0, "Rotor", HEADING_COLOUR, BLACK);
	DrawTripleString(12, 26, "Selection", HEADING_COLOUR, BLACK);

	DrawTripleString(12, 62, "Left:", TEXT_COLOUR, BLACK);
	if (current_rotor == 0) DrawTripleChar(192, 62, '1' + LeftRotor.number, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 62, '1' + LeftRotor.number, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 94, "Centre:", TEXT_COLOUR, BLACK);
	if (current_rotor == 1) DrawTripleChar(192, 94, '1' + CentreRotor.number, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 94, '1' + CentreRotor.number, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 126, "Right:", TEXT_COLOUR, BLACK);
	if (current_rotor == 2) DrawTripleChar(192, 126, '1' + RightRotor.number, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 126, '1' + RightRotor.number, TEXT_COLOUR, BLACK);

	DrawDoubleString(8, 168, "Up & down keys", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 186, "to move cursor", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 204, " 1 - 8 to set ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);
}

static void init_rotor()
{
	switch (current_rotor)
	{
	case 0:
		LeftRotor.ring = LeftRotor.initial = 0;
		break;
	case 1:
		CentreRotor.ring = CentreRotor.initial = 0;
		break;
	case 2:
		RightRotor.ring = RightRotor.initial = 0;
		break;
	}
}

static void inc_rotor()
{
	switch (current_rotor)
	{
	case 0:
		while (true)
		{
			if (++LeftRotor.number == 8) LeftRotor.number = 0;
			if (LeftRotor.number != CentreRotor.number && LeftRotor.number != RightRotor.number) break;
		}
		break;
	case 1:
		while (true)
		{
			if (++CentreRotor.number == 8) CentreRotor.number = 0;
			if (CentreRotor.number != LeftRotor.number && CentreRotor.number != RightRotor.number) break;
		}
		break;
	case 2:
		while (true)
		{
			if (++RightRotor.number == 8) RightRotor.number = 0;
			if (RightRotor.number != LeftRotor.number && RightRotor.number != CentreRotor.number) break;
		}
		break;
	}
	init_rotor();
	show_rotors(false);
}

static void dec_rotor()
{
	switch (current_rotor)
	{
	case 0:
		while (true)
		{
			if (--LeftRotor.number < 0) LeftRotor.number = 7;
			if (LeftRotor.number != CentreRotor.number && LeftRotor.number != RightRotor.number) break;
		}
		break;
	case 1:
		while (true)
		{
			if (--CentreRotor.number < 0) CentreRotor.number = 7;
			if (CentreRotor.number != LeftRotor.number && CentreRotor.number != RightRotor.number) break;
		}
		break;
	case 2:
		while (true)
		{
			if (--RightRotor.number < 0) RightRotor.number = 7;
			if (RightRotor.number != LeftRotor.number && RightRotor.number != CentreRotor.number) break;
		}
		break;
	}
	init_rotor();
	show_rotors(false);
}

static void set_rotor(int n)
{
	switch (current_rotor)
	{
	case 0:
		if (CentreRotor.number == n || RightRotor.number == n) return;
		LeftRotor.number = n;
		break;
	case 1:
		if (LeftRotor.number == n || RightRotor.number == n) return;
		CentreRotor.number = n;
		break;
	case 2:
		if (LeftRotor.number == n || CentreRotor.number == n) return;
		RightRotor.number = n;
		break;
	}
	init_rotor();
	show_rotors(false);
}

int SetRotors()
{
	current_rotor = 0;
	changed = false;

	show_rotors(true);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		if (key >= KEY_1 && key <= KEY_8)
		{
			set_rotor(key - KEY_1);
			changed = true;
			continue;
		}

		if (key >= KEY_KP1 && key <= KEY_KP8)
		{
			set_rotor(key - KEY_KP1);
			changed = true;
			continue;
		}

		switch (key)
		{
		case KEY_UP:
			if (--current_rotor < 0) current_rotor = 2;
			show_rotors(false);
			break;
		case KEY_DOWN:
			if (++current_rotor == 3) current_rotor = 0;
			show_rotors(false);
			break;

		case KEY_KPMINUS:
		case KEY_LEFT:
			dec_rotor();
			changed = true;
			break;
		case KEY_KPPLUS:
		case KEY_RIGHT:
			inc_rotor();
			changed = true;
			break;

		case KEY_F1:
		case KEY_F3:
		case KEY_F4:
		case KEY_F5:
		case KEY_F6:
		case KEY_F7:
		case KEY_F8:
		case KEY_F9:
			if (changed) ResetRotors();
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			if (changed) ResetRotors();
			return key;
		}
	}
}


//---------------------------------------------------------------------------
// setting the rings
//---------------------------------------------------------------------------

static void show_rings(bool clear)
{
	if (clear) ClearDisplay(BLACK);

	DrawTripleString(72, 0, "Ring", HEADING_COLOUR, BLACK);
	DrawTripleString(12, 26, "Selection", HEADING_COLOUR, BLACK);

	DrawTripleString(12, 62, "Left:", TEXT_COLOUR, BLACK);
	if (current_rotor == 0) DrawTripleChar(192, 62, 'A' + LeftRotor.ring, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 62, 'A' + LeftRotor.ring, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 94, "Centre:", TEXT_COLOUR, BLACK);
	if (current_rotor == 1) DrawTripleChar(192, 94, 'A' + CentreRotor.ring, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 94, 'A' + CentreRotor.ring, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 126, "Right:", TEXT_COLOUR, BLACK);
	if (current_rotor == 2) DrawTripleChar(192, 126, 'A' + RightRotor.ring, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 126, 'A' + RightRotor.ring, TEXT_COLOUR, BLACK);

	DrawDoubleString(8, 168, "Up & down keys", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 186, "to move cursor", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 204, " A - Z to set ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);
}

static void inc_ring()
{
	switch (current_rotor)
	{
	case 0:
		if (++LeftRotor.ring == 26) LeftRotor.ring = 0;
		break;
	case 1:
		if (++CentreRotor.ring == 26) CentreRotor.ring = 0;
		break;
	case 2:
		if (++RightRotor.ring == 26) RightRotor.ring = 0;
		break;
	}
	show_rings(false);
}

static void dec_ring()
{
	switch (current_rotor)
	{
	case 0:
		if (--LeftRotor.ring < 0) LeftRotor.ring = 25;
		break;
	case 1:
		if (--CentreRotor.ring < 0) CentreRotor.ring = 25;
		break;
	case 2:
		if (--RightRotor.ring < 0) RightRotor.ring = 25;
		break;
	}
	show_rings(false);
}

static void set_ring(int c)
{
	switch (current_rotor)
	{
	case 0:
		LeftRotor.ring = c;
		break;
	case 1:
		CentreRotor.ring = c;
		break;
	case 2:
		RightRotor.ring = c;
		break;
	}
	show_rings(false);
}

int SetRings()
{
	current_rotor = 0;
	changed = false;

	show_rings(true);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		if (key >= KEY_A && key <= KEY_Z)
		{
			set_ring(key - KEY_A);
			changed = true;
			continue;
		}

		switch (key)
		{
		case KEY_UP:
			if (--current_rotor < 0) current_rotor = 2;
			show_rings(false);
			break;
		case KEY_DOWN:
			if (++current_rotor == 3) current_rotor = 0;
			show_rings(false);
			break;

		case KEY_KPMINUS:
		case KEY_LEFT:
			dec_ring();
			changed = true;
			break;
		case KEY_KPPLUS:
		case KEY_RIGHT:
			inc_ring();
			changed = true;
			break;

		case KEY_F1:
		case KEY_F2:
		case KEY_F4:
		case KEY_F5:
		case KEY_F6:
		case KEY_F7:
		case KEY_F8:
		case KEY_F9:
			if (changed) ResetRotors();
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			if (changed) ResetRotors();
			return key;
		}
	}
}


//---------------------------------------------------------------------------
// setting the reflector
//---------------------------------------------------------------------------

static void show_reflector(bool clear)
{
	if (clear) ClearDisplay(BLACK);

	DrawTripleString(12, 0, "Reflector", HEADING_COLOUR, BLACK);
	DrawTripleString(12, 26, "Selection", HEADING_COLOUR, BLACK);

	DrawTripleChar(108, 80, 'B' + Reflector, CURSOR_COLOUR, BLACK);

	DrawDoubleString(8, 204, " B & C to set ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);
}

static void inc_reflector()
{
	Reflector ^= 1;
	show_reflector(false);
}

static void dec_reflector()
{
	Reflector ^= 1;
	show_reflector(false);
}

int SetReflector()
{
	changed = false;

	show_reflector(true);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		switch (key)
		{
		case KEY_KPMINUS:
		case KEY_LEFT:
			dec_reflector();
			changed = true;
			break;
		case KEY_KPPLUS:
		case KEY_RIGHT:
			inc_reflector();
			changed = true;
			break;

		case KEY_B:
			Reflector = 0;
			show_reflector(false);
			changed = true;
			break;
		case KEY_C:
			Reflector = 1;
			show_reflector(false);
			changed = true;
			break;

		case KEY_F1:
		case KEY_F2:
		case KEY_F3:
		case KEY_F5:
		case KEY_F6:
		case KEY_F7:
		case KEY_F8:
		case KEY_F9:
			if (changed) ResetRotors();
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			if (changed) ResetRotors();
			return key;
		}
	}
}


//---------------------------------------------------------------------------
// setting the initial rotor positions
//---------------------------------------------------------------------------

static void show_initial(bool clear)
{
	if (clear) ClearDisplay(BLACK);

	DrawTripleString(36, 0, "Initial", HEADING_COLOUR, BLACK);
	DrawTripleString(12, 26, "Positions", HEADING_COLOUR, BLACK);

	DrawTripleString(12, 62, "Left:", TEXT_COLOUR, BLACK);
	if (current_rotor == 0) DrawTripleChar(192, 62, 'A' + LeftRotor.initial, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 62, 'A' + LeftRotor.initial, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 94, "Centre:", TEXT_COLOUR, BLACK);
	if (current_rotor == 1) DrawTripleChar(192, 94, 'A' + CentreRotor.initial, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 94, 'A' + CentreRotor.initial, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 126, "Right:", TEXT_COLOUR, BLACK);
	if (current_rotor == 2) DrawTripleChar(192, 126, 'A' + RightRotor.initial, CURSOR_COLOUR, BLACK);
	else DrawTripleChar(192, 126, 'A' + RightRotor.initial, TEXT_COLOUR, BLACK);

	DrawDoubleString(8, 168, "Up & down keys", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 186, "to move cursor", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 204, " A - Z to set ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);
}

static void inc_initial()
{
	switch (current_rotor)
	{
	case 0:
		if (++LeftRotor.initial == 26) LeftRotor.initial = 0;
		break;
	case 1:
		if (++CentreRotor.initial == 26) CentreRotor.initial = 0;
		break;
	case 2:
		if (++RightRotor.initial == 26) RightRotor.initial = 0;
		break;
	}
	show_initial(false);
}

static void dec_initial()
{
	switch (current_rotor)
	{
	case 0:
		if (--LeftRotor.initial < 0) LeftRotor.initial = 25;
		break;
	case 1:
		if (--CentreRotor.initial < 0) CentreRotor.initial = 25;
		break;
	case 2:
		if (--RightRotor.initial < 0) RightRotor.initial = 25;
		break;
	}
	show_initial(false);
}

static void set_initial(int c)
{
	switch (current_rotor)
	{
	case 0:
		LeftRotor.initial = c;
		break;
	case 1:
		CentreRotor.initial = c;
		break;
	case 2:
		RightRotor.initial = c;
		break;
	}
	show_initial(false);
}

int SetInitialPositions()
{
	current_rotor = 0;
	changed = false;

	show_initial(true);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		if (key >= KEY_A && key <= KEY_Z)
		{
			set_initial(key - KEY_A);
			changed = true;
			continue;
		}

		switch (key)
		{
		case KEY_UP:
			if (--current_rotor < 0) current_rotor = 2;
			show_initial(false);
			break;
		case KEY_DOWN:
			if (++current_rotor == 3) current_rotor = 0;
			show_initial(false);
			break;

		case KEY_KPMINUS:
		case KEY_LEFT:
			dec_initial();
			changed = true;
			break;
		case KEY_KPPLUS:
		case KEY_RIGHT:
			inc_initial();
			changed = true;
			break;

		case KEY_F1:
		case KEY_F2:
		case KEY_F3:
		case KEY_F4:
		case KEY_F5:
		case KEY_F7:
		case KEY_F8:
		case KEY_F9:
			ResetRotors();
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			ResetRotors();
			return key;
		}
	}
}


// shows the current rotor positions
int ShowCurrentPositions()
{
	ClearDisplay(BLACK);

	DrawTripleString(36, 0, "Current", HEADING_COLOUR, BLACK);
	DrawTripleString(12, 26, "Positions", HEADING_COLOUR, BLACK);

	DrawTripleString(12, 62, "Left:", TEXT_COLOUR, BLACK);
	DrawTripleChar(192, 62, 'A' + LeftRotor.position, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 94, "Centre:", TEXT_COLOUR, BLACK);
	DrawTripleChar(192, 94, 'A' + CentreRotor.position, TEXT_COLOUR, BLACK);

	DrawTripleString(12, 126, "Right:", TEXT_COLOUR, BLACK);
	DrawTripleChar(192, 126, 'A' + RightRotor.position, TEXT_COLOUR, BLACK);

	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		switch (key)
		{
		case KEY_F1:
		case KEY_F2:
		case KEY_F3:
		case KEY_F4:
		case KEY_F5:
		case KEY_F6:
		case KEY_F8:
		case KEY_F9:
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			return key;
		}
	}
}


//---------------------------------------------------------------------------
// setting the plug board
//---------------------------------------------------------------------------

static int offset, row, col;

static int show_pair(int i, int x, int y, bool left, bool right)
{
	if (i >= 13)
	{
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
	}
	else
	{
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, PlugBoard[i][0] != -1 ? 'A' + PlugBoard[i][0] : '?', left ? CURSOR_COLOUR : TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, '-', TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, PlugBoard[i][1] != -1 ? 'A' + PlugBoard[i][1] : '?', right ? CURSOR_COLOUR : TEXT_COLOUR, BLACK);
		x = DrawTripleChar(x, y, ' ', TEXT_COLOUR, BLACK);
	}

	return x;
}

static void show_plugboard(bool clear)
{
	if (clear) ClearDisplay(BLACK);

	DrawTripleString(0, 0, "Plug Board", HEADING_COLOUR, BLACK);
	DrawTripleString(8, 26, "Selection", HEADING_COLOUR, BLACK);

	int x, y;
	y = 62; x = show_pair(offset * 2 + 0, 0, y, row == 0 && col == 0, row == 0 && col == 1);
	show_pair(offset * 2 + 1, x, y, row == 0 && col == 2, row == 0 && col == 3);
	y = 86; x = show_pair(offset * 2 + 2, 0, y, row == 1 && col == 0, row == 1 && col == 1);
	show_pair(offset * 2 + 3, x, y, row == 1 && col == 2, row == 1 && col == 3);
	y = 110; x = show_pair(offset * 2 + 4, 0, y, row == 2 && col == 0, row == 2 && col == 1);
	show_pair(offset * 2 + 5, x, y, row == 2 && col == 2, row == 2 && col == 3);

	DrawDoubleString(0, 150, " Arrow keys to ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 168, "  move cursor  ", HELP_COLOUR, BLACK);
	DrawDoubleString(8, 186, " A - Z to set ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 204, " DEL to delete ", HELP_COLOUR, BLACK);
	DrawDoubleString(0, 222, " ENTER to exit ", HELP_COLOUR, BLACK);
}

static void inc_plugboard()
{
	int i = (offset + row) * 2 + (col >> 1), j = col & 1;
	int c = PlugBoard[i][j];
	while (true)
	{
		if (++c == 26) c = -1;
		if (c == -1) break;
		bool used = false;
		for (int k = 0; k < 13; k++)
		{
			used = PlugBoard[k][0] == c || PlugBoard[k][1] == c;
			if (used) break;
		}
		if (!used) break;
	}
	PlugBoard[i][j] = c;
	show_plugboard(false);
}

static void dec_plugboard()
{
	int i = (offset + row) * 2 + (col >> 1), j = col & 1;
	int c = PlugBoard[i][j];
	while (true)
	{
		if (c-- == -1) c = 25;
		if (c == -1) break;
		bool used = false;
		for (int k = 0; k < 13; k++)
		{
			used = PlugBoard[k][0] == c || PlugBoard[k][1] == c;
			if (used) break;
		}
		if (!used) break;
	}
	PlugBoard[i][j] = c;
	show_plugboard(false);
}

static void set_plugboard(int c)
{
	int i = (offset + row) * 2 + (col >> 1), j = col & 1;
	if (c != -1) for (int k = 0; k < 13; k++)
	{
		if (PlugBoard[k][0] == c || PlugBoard[k][1] == c) return;
	}
	PlugBoard[i][j] = c;
	show_plugboard(false);
}

int SetPlugBoard()
{
	offset = 0;
	row = col = 0;
	changed = false;

	show_plugboard(true);

	while (true)
	{
		watchdog_update();

		ScanKeyboard();
		int key = GetKeyCode();
		if (key == -1) continue;

		if (key >= KEY_A && key <= KEY_Z)
		{
			set_plugboard(key - KEY_A);
			changed = true;
			continue;
		}

		switch (key)
		{
		case KEY_UP:
			if (--row < 0)
			{
				if (offset == 0) offset = 4, row = 2; else offset--, row = 0;
			}
			if (offset + row == 6) col &= 1;
			show_plugboard(false);
			break;
		case KEY_DOWN:
			if (++row == 3)
			{
				if (offset < 4) offset++, row = 2; else offset = 0, row = 0;
			}
			if (offset + row == 6) col &= 1;
			show_plugboard(false);
			break;
		case KEY_LEFT:
			if (offset + row == 6)
			{
				if (--col < 0) col = 1;
			}
			else
			{
				if (--col < 0) col = 3;
			}
			show_plugboard(false);
			break;
		case KEY_RIGHT:
			if (offset + row == 6)
			{
				if (++col == 2) col = 0;
			}
			else
			{
				if (++col == 4) col = 0;
			}
			show_plugboard(false);
			break;

		case KEY_KPMINUS:
			dec_plugboard();
			changed = true;
			break;
		case KEY_KPPLUS:
			inc_plugboard();
			changed = true;
			break;

		case KEY_DELETE:
		case KEY_SPACE:
			set_plugboard(-1);
			changed = true;
			break;

		case KEY_F1:
		case KEY_F2:
		case KEY_F3:
		case KEY_F4:
		case KEY_F6:
		case KEY_F7:
		case KEY_F8:
		case KEY_F9:
			if (changed) ResetRotors();
			return key;

		case KEY_ESC:
		case KEY_HOME:
			ResetRotors();
			return key;

		case KEY_ENTER:
		case KEY_KPENTER:
			if (changed) ResetRotors();
			return key;
		}
	}
}


//---------------------------------------------------------------------------
// saving/loading settings
//---------------------------------------------------------------------------

// saved settings
typedef struct
{
	uint32_t Magic;
	struct Rotor LeftRotor;
	struct Rotor CentreRotor;
	struct Rotor RightRotor;
	int Reflector;
	int PlugBoard[13][2];
} Settings;

static Settings settings;

#define MAGIC (*((uint32_t*) "ENIG"))

// loads settings from non-volatile memory
void LoadSettings()
{
	settings = *((Settings*) (XIP_BASE + SETTINGS_FLASH_OFFSET));

	if (settings.Magic != MAGIC) return;

	LeftRotor = settings.LeftRotor;
	CentreRotor = settings.CentreRotor;
	RightRotor = settings.RightRotor;
	Reflector = settings.Reflector;
	for (int i = 0; i < 13; i++)
	{
		PlugBoard[i][0] = settings.PlugBoard[i][0];
		PlugBoard[i][1] = settings.PlugBoard[i][1];
	}
}

// saves settings to non-volatile memory
void SaveSettings()
{
	settings.Magic = MAGIC;
	settings.LeftRotor = LeftRotor;
	settings.CentreRotor = CentreRotor;
	settings.RightRotor = RightRotor;
	settings.Reflector = Reflector;
	for (int i = 0; i < 13; i++)
	{
		settings.PlugBoard[i][0] = PlugBoard[i][0];
		settings.PlugBoard[i][1] = PlugBoard[i][1];
	}

	SaveFlash(SETTINGS_FLASH_OFFSET, (uint8_t*) &settings);
}

// saves data to flash
// fed with flash offset, pointer to data
void SaveFlash(uint32_t offset, uint8_t* p)
{
	uint32_t interrupts = save_and_disable_interrupts();

	flash_range_erase(offset, FLASH_SECTOR_SIZE);
	flash_range_program(offset, p, FLASH_PAGE_SIZE);

	restore_interrupts(interrupts);
}
