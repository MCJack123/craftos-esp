#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/bootldr.h"
#include "driver/hid.h"
#include "driver/storage.h"
#include "driver/vga.h"
#include "module/terminal.h"
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_tinyuf2.h>

static const char * TAG = "main";
#define spin() while (true) vTaskDelay(1000 / portTICK_PERIOD_MS)

esp_err_t _app_main(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "CraftOS-ESP starting up...");
    CHECK_CALLE(common_init(), "Could not initialize common module");
    CHECK_CALLE(bootldr_init(), "Could not initialize bootldr module");
    CHECK_CALLE(hid_init(), "Could not initialize HID module");
    CHECK_CALLE(storage_init(), "Could not initialize storage module");
    CHECK_CALLE(vga_init(), "Could not initialize VGA module");

    FILE* fp = fopen("/rom/startup.lua", "r");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Could not open /rom/startup.lua: %s (%d)", strerror(errno), errno);
        return errno;
    }
    for (int y = 0; y < FB_UHEIGHT; y++) {
        char buf[FB_UWIDTH];
        fread(buf, 1, FB_UWIDTH, fp);
        for (int x = 0; x < FB_UWIDTH; x++) {
            framebuffer[y][x] = (buf[y*FB_UWIDTH+x] << 1) | 1;
        }
    }
    fclose(fp);
    ESP_LOGI(TAG, "Finished startup.");
    return 0;
}

void app_main(void) {_app_main(); spin();}
