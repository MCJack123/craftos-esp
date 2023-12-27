#ifndef STORAGE_H
#define STORAGE_H
#include "../common.h"

extern bool diskMounted;
extern esp_err_t storage_init(void);
extern void storage_deinit(void);

#endif
