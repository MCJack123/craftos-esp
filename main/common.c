#include <esp_system.h>
#include <esp_event.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include "common.h"

static const char* const TAG = "common";

esp_err_t common_init(void) {
    esp_err_t err;
    ESP_LOGD(TAG, "Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    CHECK_CALLE(gpio_install_isr_service(ESP_INTR_FLAG_IRAM), "Could not install GPIO ISR handler");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    CHECK_CALLE(err, "Could not initialize NVS");
    CHECK_CALLE(esp_event_loop_create_default(), "Could not create event loop");
    esp_register_shutdown_handler(common_deinit);
    return ESP_OK;
}

void common_deinit(void) {
    esp_event_loop_delete_default();
    nvs_flash_deinit();
    gpio_uninstall_isr_service();
}
