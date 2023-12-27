#include <esp_system.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/rtc_io.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/gpio_sig_map.h>
#include "redstone.h"
#include "../event.h"

static gpio_num_t pins[6] = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40, GPIO_NUM_39};
static adc_oneshot_unit_handle_t adc_handle;
static TickType_t last_event = 0;

static void IRAM_ATTR redstone_isr(void*) {
    if (last_event != xTaskGetTickCountFromISR()) {
        last_event = xTaskGetTickCountFromISR();
        event_t event;
        event.type = EVENT_TYPE_REDSTONE;
        if (event_push_isr(&event) == pdTRUE) portYIELD_FROM_ISR();
    }
}

void redstone_init(void) {
    ledc_timer_config_t timer_conf;
    timer_conf.clk_cfg = LEDC_USE_APB_CLK;
    timer_conf.duty_resolution = 4;
    timer_conf.freq_hz = 5000000;
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.timer_num = LEDC_TIMER_1;
    ledc_timer_config(&timer_conf);
    ledc_channel_config_t pwm_conf;
    pwm_conf.duty = 0;
    pwm_conf.flags.output_invert = false;
    pwm_conf.hpoint = 0;
    pwm_conf.intr_type = LEDC_INTR_DISABLE;
    pwm_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_conf.timer_sel = LEDC_TIMER_1;
    for (int i = 0; i < 6; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add(pins[i], redstone_isr, NULL);
        pwm_conf.channel = i;
        pwm_conf.gpio_num = pins[i];
        ledc_channel_config(&pwm_conf);
    }
    adc_oneshot_unit_init_cfg_t adc_conf;
    adc_conf.clk_src = ADC_DIGI_CLK_SRC_DEFAULT;
    adc_conf.ulp_mode = ADC_ULP_MODE_DISABLE;
    adc_conf.unit_id = ADC_UNIT_1;
    adc_oneshot_new_unit(&adc_conf, &adc_handle);
    adc_oneshot_chan_cfg_t adc_chan_conf;
    adc_chan_conf.atten = ADC_ATTEN_DB_0;
    adc_chan_conf.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(adc_handle, 0, &adc_chan_conf);
    adc_oneshot_config_channel(adc_handle, 1, &adc_chan_conf);
    rtc_gpio_deinit(pins[0]);
    rtc_gpio_deinit(pins[1]);
    esp_register_shutdown_handler(redstone_deinit);
}

void redstone_deinit(void) {
    adc_oneshot_del_unit(adc_handle);
    for (int i = 0; i < 6; i++) {
        ledc_stop(LEDC_LOW_SPEED_MODE, i, 0);
        gpio_iomux_out(pins[i], 1, false); // GPIO
        gpio_isr_handler_remove(pins[i]);
    }
}

bool redstone_getInput(enum redstone_channel channel) {
    gpio_set_direction(pins[channel], GPIO_MODE_INPUT);
    if (channel < 2) rtc_gpio_deinit(pins[channel]);
    return gpio_get_level(pins[channel]);
}

void redstone_setOutput(enum redstone_channel channel, bool value) {
    gpio_set_direction(pins[channel], GPIO_MODE_OUTPUT);
    if (channel < 2) rtc_gpio_deinit(pins[channel]);
    esp_rom_gpio_connect_out_signal(pins[channel], LEDC_LS_SIG_OUT0_IDX + channel, false, false);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, value ? 15 : 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}

uint8_t redstone_getAnalogInput(enum redstone_channel channel) {
    gpio_set_direction(pins[channel], GPIO_MODE_INPUT);
    if (channel > 1) return gpio_get_level(pins[channel]) ? 15 : 0;
    rtc_gpio_init(pins[channel]);
    int res;
    adc_oneshot_read(adc_handle, channel, &res);
    return res >> 8;
}

void redstone_setAnalogOutput(enum redstone_channel channel, uint8_t value) {
    gpio_set_direction(pins[channel], GPIO_MODE_OUTPUT);
    if (channel < 2) rtc_gpio_deinit(pins[channel]);
    esp_rom_gpio_connect_out_signal(pins[channel], LEDC_LS_SIG_OUT0_IDX + channel, false, false);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, value);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
}
