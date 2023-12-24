#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "event.h"

static QueueHandle_t queue = NULL;

void event_push(const event_t* event) {
    if (queue == NULL) queue = xQueueCreate(256, sizeof(event_t));
    xQueueSend(queue, event, 0);
}

BaseType_t event_push_isr(const event_t* event) {
    if (queue == NULL) queue = xQueueCreate(256, sizeof(event_t));
    BaseType_t should_yield = pdFALSE;
    xQueueSendFromISR(queue, event, &should_yield);
    return should_yield;
}

void event_wait(event_t* event) {
    if (queue == NULL) queue = xQueueCreate(256, sizeof(event_t));
    xQueueReceive(queue, event, portMAX_DELAY);
}

void event_flush(void) {
    xQueueReset(queue);
}
