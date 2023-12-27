#ifndef WIFI_H
#define WIFI_H
#include "../common.h"
#include <esp_wifi_types.h>
#define wifi_deinit wifi_deinit_ // libnet80211 uses wifi_deinit

typedef struct {
    char ssid[33];
    uint8_t bars;
    wifi_auth_mode_t security;
} wifi_network_t;

typedef struct {
    bool connected;
    char ssid[33];
    uint8_t bars;
    char mode;
    wifi_auth_mode_t security;
} wifi_status_t;

extern const char* const wifi_security_names[];
extern esp_err_t wifi_init(void);
extern void wifi_deinit(void);
extern wifi_status_t wifi_status(void);
extern wifi_network_t* wifi_scan(uint16_t* len_out);
extern esp_err_t wifi_connect(const char* ssid, const char* password, const char* username);
extern esp_err_t wifi_disconnect(void);

#endif