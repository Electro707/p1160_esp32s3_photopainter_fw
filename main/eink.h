#ifndef EINK_H
#define EINK_H

#include "common.h"

#define DISPLAY_W   800
#define DISPLAY_H   480

#define DISP_FB_SIZE (DISPLAY_H*DISPLAY_W/2)         // frame buffer in bytes

typedef enum{
    EPD_COLOR_BLACK = 0,
    EPD_COLOR_WHITE = 1,
    EPD_COLOR_YELLOW = 2,
    EPD_COLOR_RED = 3,
    // EPD_COLOR_ORANGE = 4,        // looks like orange is not available
    EPD_COLOR_BLUE = 5,
    EPD_COLOR_GREEN = 6,
}dispColor_e;

/**
 * Inits the display with some init registers
 */
void dispInit(void);

/**
 * Fills the framebuffer with the specified color
 */
void dispFillColor(dispColor_e color);

/**
 * Fills the display with a checker pattern of all colors
 * 
 * @param checkerSizeLog2 The square size as a power of 2, so a value of 5 will result in a 32 pixel square
 *                        It's this way as it's better than division (can just shift), and we don't need
 *                        a precise grid, do we?
 */
void dispCheckerPattern(u32 checkerSizeLog2);

/**
 * Updates the frame buffer directly
 * 
 * @param data The buffer to update from
 * @param len The length of the data to be updated
 * @param offset The offset into the internal framebuffer
 */
int setFrameBuffRaw(u8 *data, u32 len, u32 offset);

/**
 * Updates the panel. Blocking function
 */
void dispUpdate(void);

#endif