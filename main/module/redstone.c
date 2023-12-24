#include <esp_system.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>
#include "redstone.h"
#include "../event.h"

static gpio_num_t pins[6] = {GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40, GPIO_NUM_39};
static adc_oneshot_unit_handle_t adc_handle;

static void IRAM_ATTR redstone_isr(void*) {
    event_t event;
    event.type = EVENT_TYPE_REDSTONE;
    if (event_push_isr(&event) == pdTRUE) portYIELD_FROM_ISR();
}

void redstone_init(void) {
    ledc_channel_config_t pwm_conf;
    pwm_conf.duty = 0;
    pwm_conf.flags.output_invert = false;
    pwm_conf.hpoint = 0;
    pwm_conf.intr_type = LEDC_INTR_DISABLE;
    pwm_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm_conf.timer_sel = LEDC_TIMER_0;
    for (int i = 0; i < 6; i++) {
        gpio_set_direction(pins[i], GPIO_MODE_INPUT);
        gpio_isr_handler_add(pins[i], redstone_isr, NULL);
        gpio_set_intr_type(pins[i], GPIO_INTR_ANYEDGE);
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
    adc_chan_conf.bitwidth = ADC_BITWIDTH_10;
    adc_oneshot_config_channel(adc_handle, 0, &adc_chan_conf);
    adc_oneshot_config_channel(adc_handle, 1, &adc_chan_conf);
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
    return gpio_get_level(pins[channel]);
}

void redstone_setOutput(enum redstone_channel channel, bool value) {
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, channel, value ? 255 : 0, 0);
}

uint8_t redstone_getAnalogInput(enum redstone_channel channel) {
    if (channel > 1) return gpio_get_level(pins[channel]) ? 15 : 0;
    int res;
    adc_oneshot_read(adc_handle, channel, &res);
    return res >> 6;
}

void redstone_setAnalogOutput(enum redstone_channel channel, uint8_t value) {
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, channel, (value << 4) | value, 0);
}
