#include <string.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <freertos/semphr.h>
#include "terminal.h"
#include "font.h"
#include "../driver/vga.h"

#define SCREEN_WIDTH (FB_UWIDTH/6)
#define SCREEN_HEIGHT (FB_UHEIGHT/18)
#define SCREEN_SIZE (SCREEN_WIDTH*SCREEN_HEIGHT)

static const char blinkTimerID = 'b';
static TimerHandle_t blinkTimer;
static TaskHandle_t timer;
static DRAM_ATTR uint8_t screen[SCREEN_SIZE];
static DRAM_ATTR uint8_t colors[SCREEN_SIZE];
static int cursorX = 0, cursorY = 0;
static bool cursorOn = false;
static int8_t cursorColor = -1;
static bool changed = true;

const uint8_t defaultPalette[16] = {
    0xFF, 0xE3, 0xCB, 0x5F,
    0xF7, 0x33, 0xEB, 0x03,
    0xA9, 0x2B, 0x8B, 0x5B,
    0x91, 0x75, 0xD5, 0x01
};

uint8_t palette[16];

static void terminal_task(void*) {
    while (true) {
        if (changed) {
            changed = false;
            TickType_t start = xTaskGetTickCount();
            for (int y = 0; y < SCREEN_HEIGHT * 18; y++) {
                uint8_t* line = framebuffer[y];
                for (int x = 0; x < SCREEN_WIDTH * 6; x++) {
                    const int cp = (y / 18) * SCREEN_WIDTH + (x / 6);
                    const uint8_t c = screen[cp];
                    line[x] = font_data[((c >> 4) * 9 + ((y % 18) >> 1)) * 96 + ((c & 0xF) * 6 + (x % 6))] ? palette[colors[cp] & 0x0F] : palette[colors[cp] >> 4];
                }
                int cx = cursorX;
                if (cursorOn && y >> 1 == cursorY * 9 + 6 && cx >= 0 && cx < TERM_WIDTH) memset(line + cx * 6, palette[cursorColor], 6);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static void terminal_blink_task(TimerHandle_t timer) {
    if (cursorColor >= 0) {cursorOn = !cursorOn; changed = true;}
}

void terminal_init(void) {
    memcpy(palette, defaultPalette, 16);
    xTaskCreatePinnedToCore(terminal_task, "terminal", 1536, NULL, 10, &timer, 0);
    blinkTimer = xTimerCreate("terminalBlink", pdMS_TO_TICKS(400), pdTRUE, &blinkTimerID, terminal_blink_task);
    xTimerStart(blinkTimer, 0);
    esp_register_shutdown_handler(terminal_deinit);
    terminal_clear(-1, 0xF0);
}

void terminal_deinit(void) {
    xTimerStop(blinkTimer, portMAX_DELAY);
    xTimerDelete(blinkTimer, portMAX_DELAY);
    vTaskDelete(timer);
}

void terminal_clear(int line, uint8_t col) {
    if (line < 0) {
        memset(screen, ' ', SCREEN_SIZE);
        memset(colors, col, SCREEN_SIZE);
    } else {
        memset(screen + line*SCREEN_WIDTH, ' ', SCREEN_WIDTH);
        memset(colors + line*SCREEN_WIDTH, col, SCREEN_WIDTH);
    }
    changed = true;
}

void terminal_scroll(int lines, uint8_t col) {
    if (lines > 0 ? (unsigned)lines >= SCREEN_HEIGHT : (unsigned)-lines >= SCREEN_HEIGHT) {
        // scrolling more than the height is equivalent to clearing the screen
        memset(screen, ' ', SCREEN_HEIGHT * SCREEN_WIDTH);
        memset(colors, col, SCREEN_HEIGHT * SCREEN_WIDTH);
    } else if (lines > 0) {
        memmove(screen, screen + lines * SCREEN_WIDTH, (SCREEN_HEIGHT - lines) * SCREEN_WIDTH);
        memset(screen + (SCREEN_HEIGHT - lines) * SCREEN_WIDTH, ' ', lines * SCREEN_WIDTH);
        memmove(colors, colors + lines * SCREEN_WIDTH, (SCREEN_HEIGHT - lines) * SCREEN_WIDTH);
        memset(colors + (SCREEN_HEIGHT - lines) * SCREEN_WIDTH, col, lines * SCREEN_WIDTH);
    } else if (lines < 0) {
        memmove(screen - lines * SCREEN_WIDTH, screen, (SCREEN_HEIGHT + lines) * SCREEN_WIDTH);
        memset(screen, ' ', -lines * SCREEN_WIDTH);
        memmove(colors - lines * SCREEN_WIDTH, colors, (SCREEN_HEIGHT + lines) * SCREEN_WIDTH);
        memset(colors, col, -lines * SCREEN_WIDTH);
    }
    changed = true;
}

void terminal_write(int x, int y, const uint8_t* text, int len, uint8_t col) {
    if (y < 0 || y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return;
    if (x < 0) {
        text += x;
        len -= x;
        x = 0;
    }
    if (len <= 0) return;
    if (x + len > SCREEN_WIDTH) len = SCREEN_WIDTH - x;
    if (len <= 0) return;
    memcpy(screen + y*SCREEN_WIDTH + x, text, len);
    memset(colors + y*SCREEN_WIDTH + x, col, len);
    changed = true;
}

void terminal_blit(int x, int y, const uint8_t* text, const uint8_t* col, int len) {
    if (y < 0 || y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return;
    if (x < 0) {
        text += x;
        col += x;
        len -= x;
        x = 0;
    }
    if (len <= 0) return;
    if (x + len > SCREEN_WIDTH) len = SCREEN_WIDTH - x;
    if (len <= 0) return;
    memcpy(screen + y*SCREEN_WIDTH + x, text, len);
    memcpy(colors + y*SCREEN_WIDTH + x, col, len);
    changed = true;
}

void terminal_cursor(int8_t color, int x, int y) {
    cursorColor = color;
    cursorX = x;
    cursorY = y;
    if (cursorColor < 0) cursorOn = false;
    changed = true;
}
