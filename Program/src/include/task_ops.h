
#ifndef TASK_OPS_H
#define TASK_OPS_H

#include "config.h"
#include "wdt_ops.h"

//Function Prototypes
void gpio_init(void);
void blinker_1(void *pvParameters);
void blinker_2(void *pvParameters);
void countdown(void *pvParameters);

//Watchdog Feeder
extern void (*feed_dog)(uint8_t);

//Task Handles
extern TaskHandle_t blinker_1_handle;
extern TaskHandle_t blinker_2_handle;
extern TaskHandle_t countdown_handle;

#endif
