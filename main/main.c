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

extern void badapple_main(void);
extern void machine_main(void*);

static void memory_timer(TimerHandle_t timer) {
    ESP_DRAM_LOGD(TAG, "Memory info:");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);
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
    ESP_LOGD(TAG, "Minimum free heap size: %" PRIu32 " bytes\n", esp_get_minimum_free_heap_size());
    CHECK_CALLE(wifi_init(), "Could not initialize Wi-Fi module");
    redstone_init();
    terminal_init();

    terminal_clear(-1, 0xF0);
    terminal_write_literal(0, 0, "Starting CraftOS-ESP...", 0xF4);

    /*uint16_t net_count;
    wifi_network_t* net = wifi_scan(&net_count);
    if (net == NULL) {
        ESP_LOGE(TAG, "Could not scan for Wi-Fi networks");
        return 1;
    }
    ESP_LOGI(TAG, "Available networks:");
    for (int i = 0; i < net_count; i++) {
        ESP_LOGI(TAG, "SSID '%s', %d bars, %s security", net[i].ssid, net[i].bars, wifi_security_names[net[i].security]);
    }

    if (net[0].security == WIFI_AUTH_OPEN) {
        wifi_connect(net[0].ssid, NULL, NULL);
        esp_http_client_config_t conf = {
            .url = "http://httpbin.org/headers",
            .method = HTTP_METHOD_GET,
            .is_async = false,
            .auth_type = HTTP_AUTH_TYPE_NONE,
            .use_global_ca_store = true,
            .crt_bundle_attach = esp_crt_bundle_attach
        };
        esp_http_client_handle_t handle = esp_http_client_init(&conf);
        esp_http_client_set_header(handle, "User-Agent", "CraftOS-ESP/0.0.1");
        esp_http_client_open(handle, 0);
        esp_http_client_fetch_headers(handle);
        size_t size = esp_http_client_read_response(handle, buf, 1024);
        ESP_LOGD(TAG, "Transferred %d bytes", size);
        buf[size] = 0;
        esp_http_client_close(handle);
        esp_http_client_cleanup(handle);
        printf("%s\n", buf);
    }*/

    xTimerCreate("memory", pdMS_TO_TICKS(30000), true, &memory_timer, memory_timer);

    ESP_LOGI(TAG, "Finished startup.");

    xTaskCreatePinnedToCore(machine_main, "CraftOS", 16384, NULL, tskIDLE_PRIORITY, NULL, 1);

    return 0;
}

void app_main(void) {_app_main();}
