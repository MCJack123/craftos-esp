#include <string.h>
#include <esp_netif_sntp.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include "wifi.h"
#include "../event.h"

const char* const wifi_security_names[] = {
    "Open",
    "WEP",
    "WPA (Personal)",
    "WPA2 (Personal)",
    "WPA/WPA2 (Personal)",
    "WPA/WPA2 (Enterprise)",
    "WPA3 (Personal)",
    "WPA2/WPA3 (Personal)",
    "Open (Secure)",
    "WPA3 (192-bit)"
};

static const char* const TAG = "wifi";
static esp_netif_t* netif;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;
static int s_retry_num = 0;
static bool should_retry = true;

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (!should_retry) {
        event_t event;
        event.type = EVENT_TYPE_WIFI_DISCONNECT;
        event_push(&event);
        return;
    }
    s_retry_num++;
    if (s_retry_num > 5) {
        ESP_LOGI(TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let example_wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs) {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }
        event_t event;
        event.type = EVENT_TYPE_WIFI_DISCONNECT;
        event_push(&event);
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void on_wifi_connect(void *esp_netif, esp_event_base_t event_base,
                            int32_t event_id, void *event_data)
{
    event_t event;
    event.type = EVENT_TYPE_WIFI_CONNECT;
    event_push(&event);
}

static void on_sta_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    if (s_semph_get_ip_addrs) {
        xSemaphoreGive(s_semph_get_ip_addrs);
    } else {
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
}

esp_err_t wifi_init(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "Initializing wifi module");
    wifi_init_config_t conf = WIFI_INIT_CONFIG_DEFAULT();
    CHECK_CALLE(esp_wifi_init(&conf), "Failed to initialize Wi-Fi");
    CHECK_CALLE(esp_wifi_set_mode(WIFI_MODE_STA), "Failed to set Wi-Fi mode");
    CHECK_CALLE(esp_netif_init(), "Could not initialize network interface");
    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    esp_netif_config.if_desc = "Wi-Fi";
    esp_netif_config.route_prio = 128;
    netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    CHECK_CALLE(esp_wifi_set_default_wifi_sta_handlers(), "Failed to set default Wi-Fi STA handlers");
    CHECK_CALLE(esp_wifi_start(), "Failed to start Wi-Fi");
    CHECK_CALLE(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL), "Could not set event handler");
    CHECK_CALLE(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_sta_got_ip, NULL), "Could not set event handler");
    CHECK_CALLE(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &on_wifi_connect, netif), "Could not set event handler");
    esp_wifi_connect();
    esp_register_shutdown_handler(wifi_deinit);
    return ESP_OK;
}

void wifi_deinit(void) {
    esp_wifi_stop();
    esp_wifi_deinit();
}

wifi_status_t wifi_status(void) {
    wifi_status_t retval;
    wifi_ap_record_t info;
    if (esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        retval.connected = false;
        return retval;
    }
    retval.connected = true;
    strncpy(retval.ssid, (char*)info.ssid, 33);
    retval.bars = (info.rssi + 90) / 10;
    if (retval.bars > 4) retval.bars = 4;
    retval.security = info.authmode;
    retval.mode = info.phy_11ax ? 'x' : (info.phy_11n ? 'n' : (info.phy_11g ? 'g' : (info.phy_11b ? 'b' : 'a')));
    return retval;
}

wifi_network_t* wifi_scan(uint16_t* len_out) {
    esp_err_t err;
    if ((err = esp_wifi_scan_start(NULL, true))!= ESP_OK) {
        ESP_LOGE(TAG, "Could not scan Wi-Fi networks: %s (%d)", esp_err_to_name(err), err);
        return NULL;
    }
    esp_wifi_scan_get_ap_num(len_out);
    wifi_ap_record_t* records = heap_caps_malloc(*len_out * sizeof(wifi_ap_record_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    if ((err = esp_wifi_scan_get_ap_records(len_out, records)) != ESP_OK) {
        ESP_LOGE(TAG, "Could not scan Wi-Fi networks: %s (%d)", esp_err_to_name(err), err);
        free(records);
        return NULL;
    }
    wifi_network_t* buf = heap_caps_malloc(*len_out * sizeof(wifi_network_t), MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    for (uint16_t i = 0; i < *len_out; i++) {
        strcpy(buf[i].ssid, (char*)records[i].ssid);
        buf[i].bars = (records[i].rssi + 90) / 10;
        if (buf[i].bars > 4) buf[i].bars = 4;
        buf[i].security = records[i].authmode;
    }
    free(records);
    return buf;
}

esp_err_t wifi_connect(const char* ssid, const char* password, const char* username) {
    esp_err_t err;
    ESP_LOGI(TAG, "Connecting to Wi-Fi network '%s'...", ssid);
    wifi_sta_config_t conf;
    memset(&conf, 0, sizeof(conf));
    strcpy((char*)conf.ssid, ssid);
    if (password) strcpy((char*)conf.password, password);
    should_retry = true;
    conf.scan_method = WIFI_ALL_CHANNEL_SCAN;
    conf.bssid_set = 0;
    conf.failure_retry_cnt = 0;
    conf.sort_method = password ? WIFI_CONNECT_AP_BY_SECURITY : WIFI_CONNECT_AP_BY_SIGNAL;
    conf.pmf_cfg.required = false;
    conf.rm_enabled = conf.btm_enabled = conf.ft_enabled = conf.mbo_enabled = conf.owe_enabled = 1;
    CHECK_CALLE(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t*)&conf), "Could not set Wi-Fi config");
    s_semph_get_ip_addrs = xSemaphoreCreateBinary();
    CHECK_CALLE(esp_wifi_connect(), "Could not connect to Wi-Fi network");
    xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
    vSemaphoreDelete(s_semph_get_ip_addrs);
    s_semph_get_ip_addrs = NULL;
    if (s_retry_num > 5) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t wifi_disconnect(void) {
    should_retry = false;
    return esp_wifi_disconnect();
}
