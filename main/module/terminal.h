#ifndef TERMINAL_H
#define TERMINAL_H
#include "../common.h"

#define TERM_WIDTH 53
#define TERM_HEIGHT 22
#define NO_CURSOR -1

extern void terminal_init(void);
extern void terminal_deinit(void);
extern void terminal_clear(int line, uint8_t colors);
extern void terminal_scroll(int lines, uint8_t colors);
extern void terminal_write(int x, int y, const uint8_t* text, int len, uint8_t colors);
#define terminal_write_string(x, y, text, colors) terminal_write(x, y, (const uint8_t*)(text), strlen(text), colors)
#define terminal_write_literal(x, y, text, colors) terminal_write(x, y, (const uint8_t*)text, sizeof(text)-1, colors)
extern void terminal_blit(int x, int y, const uint8_t* text, const uint8_t* colors, int len);
extern void terminal_cursor(int8_t color, int x, int y);

#endif