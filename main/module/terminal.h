#ifndef TERMINAL_H
#define TERMINAL_H
#include "../common.h"

extern void terminal_init(void);
extern void terminal_deinit(void);
extern void terminal_clear(int line, uint8_t colors);
extern void terminal_scroll(int lines, uint8_t colors);
extern void terminal_write(int x, int y, uint8_t* text, int len, uint8_t colors);
extern void terminal_blit(int x, int y, uint8_t* text, uint8_t* colors, int len);

#endif