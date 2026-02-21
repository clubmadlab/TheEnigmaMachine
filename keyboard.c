/****************************************************************************
* FILE:      keyboard.c														*
* CONTENTS:  USB keyboard handler											*
* COPYRIGHT: MadLab Ltd. 2025												*
* AUTHOR:    James Hutchby													*
* UPDATED:   04/01/25														*
****************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "bsp/board.h"
#include "tusb.h"

#include "common.h"
#include "main.h"
#include "keyboard.h"
#include "usb_hid_keys.h"


//---------------------------------------------------------------------------
// constants
//---------------------------------------------------------------------------

#define KBD_FLAG_SHIFT (1<<0)
#define KBD_FLAG_CONTROL (1<<1)
#define KBD_FLAG_ALT (1<<2)


//---------------------------------------------------------------------------
// variables
//---------------------------------------------------------------------------

#define KEY_BUFFER_SIZE 16
static int key_buffer[KEY_BUFFER_SIZE];

static int key_count;

bool KeyboardDetected = false;


//---------------------------------------------------------------------------
// functions
//---------------------------------------------------------------------------

// initialises the keyboard
void InitKeyboard()
{
	// empty key FIFO buffer
	key_count = 0;

	board_init();
	tusb_init();
}

// scans the keyboard
void ScanKeyboard()
{
	tuh_task();
}

static int remove_key()
{
	// if empty
	if (key_count == 0) return -1;

	int code = key_buffer[0];

	for (int i = 0; i < KEY_BUFFER_SIZE-1; i++) key_buffer[i] = key_buffer[i+1];
	key_count--;

	return code;
}

static void add_key(int code)
{
	// if full
	if (key_count == KEY_BUFFER_SIZE) remove_key();

	key_buffer[key_count++] = code;
}

static void kbd_raw_key_down(int code, int flags)
{
	add_key(code);
}

// returns key pressed, or -1 if no key pressed
int GetKeyCode()
{
	return remove_key();
}

static inline bool is_key_held(const hid_keyboard_report_t* report, uint8_t keycode)
{
	for (uint8_t i = 0; i < 6; i++)
	{
		if (report->keycode[i] == keycode) return true;
	}
	return false;
}

// The report will contain up to 6 keystrokes, each representing a different key that is pressed. Filter duplicate
// keystrokes that arise when multiple keys are pressed such that one key is pressed before another is released.
static void process_kbd_report(const hid_keyboard_report_t* report)
{
	static hid_keyboard_report_t prev_report = {0, 0, {0}};

	for (uint8_t i = 0; i < 6; i++)
	{
		if (report->keycode[i] != 0 && !is_key_held(&prev_report, report->keycode[i]))
		{
			bool const is_shift_pressed = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
			bool const is_ctrl_pressed = report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL);
			bool const is_alt_pressed = report->modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT);
			int code = report->keycode[i];
			int flags = 0;
			if (is_shift_pressed) flags |= KBD_FLAG_SHIFT;
			if (is_ctrl_pressed) flags |= KBD_FLAG_CONTROL;
			if (is_alt_pressed) flags |= KBD_FLAG_ALT;
			kbd_raw_key_down(code, flags);
		}
	}
	prev_report = *report;
}

// called when a USB device is attached
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, const uint8_t* desc_report, uint16_t desc_len)
{
	FlashLED();
	FlashLED();

	if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD)
	{
		tuh_hid_receive_report(dev_addr, instance);
	}

	KeyboardDetected = true;
}

// called when USB data is received
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, const uint8_t* report, uint16_t len)
{
	if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD)
	{
		process_kbd_report((const hid_keyboard_report_t*) report);
		tuh_hid_receive_report(dev_addr, instance);
	}
}

// called when a USB device is removed
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
}
