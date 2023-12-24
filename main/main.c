#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <esp_chip_info.h>
#include <esp_crt_bundle.h>
#include <esp_flash.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
static char buf[1024];

extern void badapple_main(void);

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

    int x = 0, y = 0;
    uint8_t colors = 0xF0;
    terminal_clear(-1, colors);

    uint16_t net_count;
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
    }

    ESP_LOGI(TAG, "Finished startup.");

    terminal_cursor(0, x, y);
    while (true) {
        event_t event;
        event_wait(&event);
        char space = ' ';
        switch (event.type) {
            case EVENT_TYPE_KEY:
                ESP_LOGD(TAG, "Got key %d", event.key.keycode);
                switch (event.key.keycode) {
                    case 28:
                        x = 0;
                        if (++y == TERM_HEIGHT) {
                            terminal_scroll(1, colors);
                            y--;
                        }
                        terminal_cursor(0, x, y);
                        break;
                    case 14:
                        if (x == 0 && y == 0) break;
                        if (--x < 0) {
                            y--;
                            x = TERM_WIDTH - 1;
                        }
                        terminal_write(x, y, (uint8_t*)&space, 1, colors);
                        terminal_cursor(0, x, y);
                        break;
                }
                break;
            case EVENT_TYPE_CHAR:
                ESP_LOGD(TAG, "Got character %c (%hhd)", event.character.c, event.character.c);
                terminal_write(x++, y, (uint8_t*)&event.character.c, 1, colors);
                if (x == TERM_WIDTH) {
                    x = 0;
                    if (++y == TERM_HEIGHT) {
                        terminal_scroll(1, colors);
                        y--;
                    }
                }
                terminal_cursor(0, x, y);
                break;
            default:
                ESP_LOGD(TAG, "Got event type %lu", event.type);
                break;
        }
    }

    return 0;
}

void app_main(void) {_app_main();}
