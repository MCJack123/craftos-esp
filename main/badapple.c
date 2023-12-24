#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <esp_timer.h>
#include <pwm_audio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "event.h"
#include "driver/audio.h"
#include "driver/vga.h"

static const char* const TAG = "badapple";
static TickType_t start;

static void audio_task(void*) {
    FILE* fp = fopen("/disk/badapple.wav", "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Could not open /disk/badapple.wav (%d)", errno);
        vTaskDelete(NULL);
        return;
    }
    fseek(fp, 44, SEEK_SET);
    for (int i = 0; !feof(fp); i++) {
        if (i == 24) {
            while (true) {
                event_t ev;
                event_wait(&ev);
                if (ev.type == EVENT_TYPE_SPEAKER_AUDIO_EMPTY) break;
            }
        }
        uint8_t* data = malloc(4096);
        size_t sz = fread(data, 1, 4096, fp);
        audio_queue(data, sz);
    }
    fclose(fp);
    vTaskDelete(NULL);
}

static int lastFrame = 0;
static void draw_frame(TimerHandle_t timer) {
    FILE* fp = pvTimerGetTimerID(timer);
    if (lastFrame > pdTICKS_TO_MS(xTaskGetTickCount() - start) / 16.666667) return;
    for (; lastFrame < pdTICKS_TO_MS(xTaskGetTickCount() - start) / 16.666667; lastFrame++) {
        uint16_t nbytes;
        fread(&nbytes, 2, 1, fp);
        fseek(fp, nbytes, SEEK_CUR);
    }
    int x = 0, y = 0;
    uint8_t color = 1;
    uint16_t nbytes;
    fread(&nbytes, 2, 1, fp);
    while (y < 240) {
        int n = fgetc(fp);
        if (n & 0x80) n = (n & 0x7F) | (fgetc(fp) << 7);
        for (int i = 0; i < n; i++) {
            if (y >= 20 && y < 220) {
                framebuffer[(y-20)*2][x] = color;
                framebuffer[(y-20)*2+1][x] = color;
            }
            if (++x == 320) {
                x = 0;
                y++;
            }
        }
        if (n != 0x7FFF) color = ~color | 1;
    }
    lastFrame++;
}

void badapple_main(void) {
    TaskHandle_t audio;
    char magic[5] = {0};
    uint16_t nframes;
    FILE* fp = fopen("/disk/badapple.mvd", "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Could not open /disk/badapple.mvd (%d)", errno);
        vTaskDelete(audio);
        return;
    }
    fread(magic, 4, 1, fp);
    if (strcmp(magic, "MVIE")) {
        ESP_LOGE(TAG, "Bad magic in video file");
        fclose(fp);
        vTaskDelete(audio);
        return;
    }
    fseek(fp, 6, SEEK_CUR);
    fread(&nframes, 2, 1, fp);
    TimerHandle_t timer = xTimerCreate("badapple video", pdMS_TO_TICKS(16), true, fp, draw_frame);
    start = xTaskGetTickCount();
    xTimerStart(timer, 0);
    xTaskCreate(audio_task, "badapple audio", 4096, NULL, 5, &audio);
    while (lastFrame < nframes) vTaskDelay(pdMS_TO_TICKS(1000));
    xTimerStop(timer, portMAX_DELAY);
    xTimerDelete(timer, portMAX_DELAY);
    fclose(fp);
}
