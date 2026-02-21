/****************************************************************************
* FILE:      common.h														*
* CONTENTS:  Common definitions												*
* COPYRIGHT: MadLab Ltd. 2025												*
* AUTHOR:    James Hutchby													*
* UPDATED:   06/01/25														*
****************************************************************************/

#include "pico/stdlib.h"
#include "hardware/flash.h"


#define WIDTH 240				// display width in pixels
#define HEIGHT 240				// display height in pixels

// standard colours
#define BLACK 0x0000
#define BLUE 0x001f
#define RED 0xf800
#define GREEN 0x07e0
#define CYAN 0x07ff
#define MAGENTA 0xf81f
#define YELLOW 0xffe0
#define WHITE 0xffff
#define ORANGE 0xfc00

// menu colours
#define MENU_COLOUR BLUE
#define HEADING_COLOUR BLUE
#define TEXT_COLOUR BLUE
#define CURSOR_COLOUR RED
#define HELP_COLOUR CYAN

// flash address (last sector in memory)
#define SETTINGS_FLASH_OFFSET (0x1ffL * FLASH_SECTOR_SIZE)
