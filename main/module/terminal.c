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

static const char timerID = 't';
static const char blinkTimerID = 'b';
static TimerHandle_t timer, blinkTimer;
static SemaphoreHandle_t mutex;
static uint8_t screen[SCREEN_SIZE];
static uint8_t colors[SCREEN_SIZE];
static uint8_t screen_buf[SCREEN_SIZE];
static uint8_t colors_buf[SCREEN_SIZE];
static int cursorX = 0, cursorY = 0;
static bool cursorOn = false;
static int8_t cursorColor = -1;

static uint8_t palette[16] = {
    0xFF, 0xE3, 0xCB, 0x5F,
    0xF7, 0x33, 0xEB, 0x03,
    0xA9, 0x7B, 0x8B, 0x5B,
    0x91, 0x75, 0xB5, 0x01
};

static void terminal_task(TimerHandle_t timer) {
    //if (!xSemaphoreTakeFromISR(mutex, 50 / portTICK_PERIOD_MS)) return;
    memcpy(screen_buf, screen, SCREEN_SIZE);
    memcpy(colors_buf, colors, SCREEN_SIZE);
    BaseType_t should_yield;
    //xSemaphoreGiveFromISR(mutex, &should_yield);
    //if (should_yield == pdTRUE) portYIELD_FROM_ISR();
    for (int y = 0; y < SCREEN_HEIGHT * 18; y++) {
        uint8_t* line = framebuffer[y];
        for (int x = 0; x < SCREEN_WIDTH * 6; x++) {
            const int cp = (y / 18) * SCREEN_WIDTH + (x / 6);
            const uint8_t c = screen[cp];
            line[x] = font_data[((c >> 4) * 9 + ((y % 18) >> 1)) * 96 + ((c & 0xF) * 6 + (x % 6))] ? palette[colors[cp] & 0x0F] : palette[colors[cp] >> 4];
        }
        if (cursorOn && y >> 1 == cursorY * 9 + 6 && cursorX >= 0 && cursorX < TERM_WIDTH) memset(line + cursorX * 6, palette[cursorColor], 6);
    }
}

static void terminal_blink_task(TimerHandle_t timer) {
    if (cursorColor >= 0) cursorOn = !cursorOn;
}

void terminal_init(void) {
    mutex = xSemaphoreCreateMutex();
    timer = xTimerCreate("terminal", pdMS_TO_TICKS(50), pdTRUE, &timerID, terminal_task);
    xTimerStart(timer, pdMS_TO_TICKS(50));
    blinkTimer = xTimerCreate("terminalBlink", pdMS_TO_TICKS(400), pdTRUE, &blinkTimerID, terminal_blink_task);
    xTimerStart(blinkTimer, 0);
    esp_register_shutdown_handler(terminal_deinit);
    terminal_clear(-1, 0x0F);
}

void terminal_deinit(void) {
    xTimerStop(blinkTimer, portMAX_DELAY);
    xTimerDelete(blinkTimer, portMAX_DELAY);
    xTimerStop(timer, portMAX_DELAY);
    xTimerDelete(timer, portMAX_DELAY);
}

void terminal_clear(int line, uint8_t col) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (line < 0) {
        memset(screen, ' ', SCREEN_SIZE);
        memset(colors, col, SCREEN_SIZE);
    } else {
        memset(screen + line*SCREEN_WIDTH, ' ', SCREEN_WIDTH);
        memset(colors + line*SCREEN_WIDTH, col, SCREEN_WIDTH);
    }
    xSemaphoreGive(mutex);
}

void terminal_scroll(int lines, uint8_t col) {
    xSemaphoreTake(mutex, portMAX_DELAY);
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
    xSemaphoreGive(mutex);
}

void terminal_write(int x, int y, uint8_t* text, int len, uint8_t col) {
    if (y < 0 || y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return;
    if (x < 0) {
        text -= x;
        len += x;
        x = 0;
    }
    if (x + len > SCREEN_WIDTH) len = SCREEN_WIDTH - x;
    xSemaphoreTake(mutex, portMAX_DELAY);
    memcpy(screen + y*SCREEN_WIDTH + x, text, len);
    memset(colors + y*SCREEN_WIDTH + x, col, len);
    xSemaphoreGive(mutex);
}

void terminal_blit(int x, int y, uint8_t* text, uint8_t* col, int len) {
    if (y < 0 || y >= SCREEN_HEIGHT || x >= SCREEN_WIDTH) return;
    if (x < 0) {
        text -= x;
        col -= x;
        len += x;
        x = 0;
    }
    if (x + len > SCREEN_WIDTH) len = SCREEN_WIDTH - x;
    if (len <= 0) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    memcpy(screen + y*SCREEN_WIDTH + x, text, len);
    memcpy(colors + y*SCREEN_WIDTH + x, col, len);
    xSemaphoreGive(mutex);
}

void terminal_cursor(int8_t color, int x, int y) {
    cursorColor = color;
    cursorX = x;
    cursorY = y;
    if (cursorColor < 0) cursorOn = false;
}
