#ifndef AUDIO_H
#define AUDIO_H
#include "../common.h"
#include <pwm_audio.h>

extern esp_err_t audio_init(void);
extern void audio_deinit(void);
extern bool audio_queue(const uint8_t* buf, size_t len);

#endif