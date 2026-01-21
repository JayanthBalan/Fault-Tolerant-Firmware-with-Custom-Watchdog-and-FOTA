
#ifndef WDT_OPS_H
#define WDT_OPS_H

#include "config.h"
#include "esp_ota_ops.h"

//Function Prototypes
void watchdog(void *pvParameters);
void wdt_feed(uint8_t);
uint8_t task_register(const char*, uint32_t, uint8_t);
void task_helper(TaskHandle_t, uint8_t);

extern TaskHandle_t watchdog_handle; //Handles

#endif
