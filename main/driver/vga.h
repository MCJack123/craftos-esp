#ifndef VGA_H
#define VGA_H
#include "../common.h"

#define FB_WIDTH    416
#define FB_HEIGHT   445
#define FB_FLINE    4
#define FB_UWIDTH   320
#define FB_UHEIGHT  400

extern uint8_t* framebuffer[FB_UHEIGHT];

extern esp_err_t vga_init(void);
extern void vga_deinit(void);

#endif
