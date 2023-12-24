#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "audio.h"
#include "../event.h"

static const char* const TAG = "audio";
static QueueHandle_t audioQueue;
static TaskHandle_t audioTask;
static int64_t start;

struct audio_queue {
    const uint8_t* buf;
    size_t sz;
};

static void audio_task(void*) {
    start = esp_timer_get_time();
    while (true) {
        struct audio_queue audio, next;
        if (xQueueReceive(audioQueue, &audio, 0) == pdFALSE) {
            xQueueReceive(audioQueue, &audio, portMAX_DELAY);
            start = esp_timer_get_time();
        }
        if (xQueuePeek(audioQueue, &next, 0) == pdFALSE) {
            event_t event;
            event.type = EVENT_TYPE_SPEAKER_AUDIO_EMPTY;
            event_push(&event);
        }
        const uint8_t* buf = audio.buf;
        size_t sz = audio.sz;
        int64_t target = start + (sz * 1000 / 48);
        do {
            size_t written;
            pwm_audio_write(buf, sz, &written, portMAX_DELAY);
            buf += written;
            sz -= written;
        } while (sz > 0);
        free(audio.buf);
        vTaskDelay(1);
        while (esp_timer_get_time() < target) vTaskDelay(1);
        start = target;
    }
}

esp_err_t audio_init(void) {
    esp_err_t err;
    ESP_LOGI(TAG, "Initializing audio module");
    pwm_audio_config_t conf;
    conf.duty_resolution = LEDC_TIMER_8_BIT;
    conf.gpio_num_left = GPIO_NUM_21;
    conf.ledc_channel_left = LEDC_CHANNEL_7;
    conf.ledc_timer_sel = LEDC_TIMER_0;
    conf.ringbuf_len = 24000;
    conf.gpio_num_right = -1;
    conf.ledc_channel_right = -1;
    CHECK_CALLE(pwm_audio_init(&conf), "Could not initialize audio");
    CHECK_CALLE(pwm_audio_set_param(48000, 8, 1), "Could not set parameters");
    CHECK_CALLE(pwm_audio_start(), "Could not start audio");
    gpio_set_drive_capability(GPIO_NUM_21, GPIO_DRIVE_CAP_3);
    audioQueue = xQueueCreate(32, sizeof(struct audio_queue));
    if (!audioQueue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(audio_task, "audio", 2048, NULL, 15, &audioTask) != pdTRUE) return ESP_ERR_NO_MEM;
    esp_register_shutdown_handler(audio_deinit);
    return ESP_OK;
}

void audio_deinit(void) {
    vTaskDelete(audioTask);
    pwm_audio_stop();
    pwm_audio_deinit();
}

bool audio_queue(const uint8_t* buf, size_t len) {
    struct audio_queue audio;
    audio.buf = buf;
    audio.sz = len;
    return xQueueSend(audioQueue, &audio, pdMS_TO_TICKS(50)) == pdTRUE;
}
