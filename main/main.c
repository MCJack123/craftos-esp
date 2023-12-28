#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include "event.h"
#include "driver/audio.h"
#include "driver/bootldr.h"
#include "driver/hid.h"
#include "driver/storage.h"
#include "driver/vga.h"
#include "driver/wifi.h"
#include "module/redstone.h"
#include "module/terminal.h"

static const char * TAG = "main";
static TaskHandle_t machineTask;

extern void badapple_main(void);
extern void machine_main(void*);

static void memory_timer(TimerHandle_t timer) {
    ESP_DRAM_LOGD(TAG, "Memory info:");
    if (esp_log_default_level >= ESP_LOG_DEBUG) {
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
        heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
    }
}

static void shutdown(void) {
    if (xTaskGetCurrentTaskHandle() != machineTask) vTaskDelete(machineTask);
}

esp_err_t _app_main(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "CraftOS-ESP starting up...");
    CHECK_CALLE(common_init(), "Could not initialize common module");
    CHECK_CALLE(bootldr_init(), "Could not initialize bootldr module");
    CHECK_CALLE(audio_init(), "Could not initialize audio module");
    CHECK_CALLE(hid_init(), "Could not initialize HID module");
    CHECK_CALLE(storage_init(), "Could not initialize storage module");
    CHECK_CALLE(vga_init(), "Could not initialize VGA module");
    CHECK_CALLE(wifi_init(), "Could not initialize Wi-Fi module");
    redstone_init();
    terminal_init();

    terminal_clear(-1, 0xF0);
    terminal_write_literal(0, 0, "Starting CraftOS-ESP...", 0xF4);

    xTimerStart(xTimerCreate("memory", pdMS_TO_TICKS(30000), true, &memory_timer, memory_timer), portMAX_DELAY);

    ESP_LOGI(TAG, "Finished startup.");

    xTaskCreatePinnedToCore(machine_main, "CraftOS", 16384, NULL, tskIDLE_PRIORITY, &machineTask, 1);
    esp_register_shutdown_handler(shutdown);

    return 0;
}

void app_main(void) {_app_main();}
