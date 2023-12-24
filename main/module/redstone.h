#ifndef REDSTONE_H
#define REDSTONE_H
#include <stdbool.h>
#include "../common.h"

enum redstone_channel {
    REDSTONE_CHANNEL_LEFT = 0,
    REDSTONE_CHANNEL_RIGHT,
    REDSTONE_CHANNEL_TOP,
    REDSTONE_CHANNEL_BOTTOM,
    REDSTONE_CHANNEL_FRONT,
    REDSTONE_CHANNEL_BACK
};

extern void redstone_init(void);
extern void redstone_deinit(void);
extern bool redstone_getInput(enum redstone_channel channel);
extern void redstone_setOutput(enum redstone_channel channel, bool value);
extern uint8_t redstone_getAnalogInput(enum redstone_channel channel);
extern void redstone_setAnalogOutput(enum redstone_channel channel, uint8_t value);

#endif