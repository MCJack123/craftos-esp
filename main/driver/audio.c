#include "audio.h"

static const char* const TAG = "audio";

esp_err_t audio_init(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "Initializing audio module");
    pwm_audio_config_t conf;
    conf.duty_resolution = LEDC_TIMER_8_BIT;
    conf.gpio_num_left = GPIO_NUM_21;
    conf.ledc_channel_left = LEDC_CHANNEL_7;
    conf.ledc_timer_sel = LEDC_TIMER_0;
    conf.ringbuf_len = 4800;
    conf.gpio_num_right = -1;
    conf.ledc_channel_right = -1;
    CHECK_CALLE(pwm_audio_init(&conf), "Could not initialize audio");
    CHECK_CALLE(pwm_audio_set_param(48000, 8, 1), "Could not set parameters");
    CHECK_CALLE(pwm_audio_start(), "Could not start audio");
    gpio_set_drive_capability(GPIO_NUM_21, GPIO_DRIVE_CAP_3);
    esp_register_shutdown_handler(audio_deinit);
    return ESP_OK;
}

void audio_deinit(void) {
    pwm_audio_stop();
    pwm_audio_deinit();
}
