#include <time.h>
//#include <esp_tinyuf2.h>
#include <esp_system.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "bootldr.h"

static const char* const TAG = "bootldr";
static time_t lastpress = 0;

static IRAM_ATTR void bootldr_button(void*) {
    time_t tm = time(NULL);
    if (lastpress - tm < 2) {
        esp_err_t err;
        nvs_handle_t nvs;
        err = nvs_open("bootldr", NVS_READWRITE, &nvs);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not open bootloader state: %s (%d)", esp_err_to_name(err), err);
            return;
        }
        nvs_set_u8(nvs, "status", 1);
        nvs_commit(nvs);
        nvs_close(nvs);
        esp_restart();
        return; // for safety
    }
    lastpress = tm;
}

esp_err_t bootldr_init(void) {
    esp_err_t err;
    nvs_handle_t nvs;
    uint8_t status;
    ESP_LOGI(TAG, "Initializing bootldr module");
    if (esp_reset_reason() == ESP_RST_SW) do {
        err = nvs_open("bootldr", NVS_READWRITE, &nvs);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not open bootloader state: %s (%d)", esp_err_to_name(err), err);
            break;
        }
        err = nvs_get_u8(nvs, "status", &status);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not read bootloader state: %s (%d)", esp_err_to_name(err), err);
            break;
        }
        if (status) {
            nvs_set_u8(nvs, "status", 0);
            nvs_commit(nvs);
            nvs_close(nvs);
            common_deinit();
            /*tinyuf2_ota_config_t ota_conf = DEFAULT_TINYUF2_OTA_CONFIG();
            ota_conf.complete_cb = NULL;
            ota_conf.if_restart = true;
            tinyuf2_nvs_config_t nvs_conf = DEFAULT_TINYUF2_NVS_CONFIG();
            esp_tinyuf2_install(&ota_conf, &nvs_conf);
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            esp_tinyuf2_uninstall();*/
            esp_restart();
        }
    } while (false);
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_intr_type(GPIO_NUM_0, GPIO_INTR_NEGEDGE);
    gpio_isr_handler_add(GPIO_NUM_0, bootldr_button, NULL);
    esp_register_shutdown_handler(bootldr_deinit);
    return ESP_OK;
}

void bootldr_deinit(void) {
    gpio_isr_handler_remove(GPIO_NUM_0);
}
