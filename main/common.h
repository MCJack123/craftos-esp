#ifndef COMMON_H
#define COMMON_H
#include <esp_err.h>
#include <esp_log.h>

#define CHECK_CALLW(call, msg) if ((err = call) != ESP_OK) {ESP_LOGW(TAG, msg ": %s (%d)", esp_err_to_name(err), err); return 0;}
#define CHECK_CALLE(call, msg) if ((err = call) != ESP_OK) {ESP_LOGE(TAG, msg ": %s (%d)", esp_err_to_name(err), err); return err;}

extern esp_err_t common_init(void);
extern void common_deinit(void);

#endif