
#include "wdt_ops.h"

#define MAX_TASKS 64
#define MONITOR_INTERVAL 1000  //ms

typedef struct {
    TaskHandle_t handle;
    const char *name;
    TickType_t last_checkin_tick; //Last Feed Timestamp
    TickType_t timeout_ticks; //Timeout Interval
    bool critical; //Rollback = true; Restart = false
    bool active; //Status
} wdt_task_t;

static const char *TAG = "WDT_OPS";
static wdt_task_t tasks[MAX_TASKS];
static uint8_t task_count = 0;
TaskHandle_t watchdog_handle;

//Register Targets
uint8_t task_register(const char *name, uint32_t tim, uint8_t crit) {
    if (task_count < MAX_TASKS) {
        tasks[task_count].handle = NULL;
        tasks[task_count].name = name;
        tasks[task_count].timeout_ticks = pdMS_TO_TICKS(tim);
        tasks[task_count].last_checkin_tick = xTaskGetTickCount();
        tasks[task_count].critical = crit;
        tasks[task_count].active = true;
        task_count++;
        return task_count - 1; //Task ID
    }
    return 255; //MAX_TASKS Reached
}

void task_helper(TaskHandle_t handle, uint8_t id)
{
    if(id < task_count)
    {
        tasks[id].handle = handle;
    }
}

//Timestamp update
void wdt_feed(uint8_t task_id) {
    if (task_id < task_count) {
        tasks[task_id].last_checkin_tick = xTaskGetTickCount();
    }
}

//Periodic Monitor
void watchdog(void *pvParameters) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL)); 
        TickType_t curr_tick = xTaskGetTickCount();

        for (uint8_t i = 0; i < task_count; i++) {
            if (tasks[i].active) {
                uint32_t elapsed = pdTICKS_TO_MS(curr_tick - tasks[i].last_checkin_tick);
                uint32_t limit = pdTICKS_TO_MS(tasks[i].timeout_ticks);
                if (elapsed > limit) {
                    ESP_LOGE(TAG, "Unresponsive Task: Name = %s, ID = %hhu || Threshold = %lu, Elapsed Time = %lu", tasks[i].name, (uint8_t)i, limit, elapsed);
                    if (tasks[i].critical) {
                        ESP_LOGE(TAG, "Critical Task Fail: Rollback");
                        esp_ota_mark_app_invalid_rollback_and_reboot();
                    }
                    else {
                        ESP_LOGE(TAG, "Non-Critical Task Fail: Terminating Task %s", tasks[i].name);
                        tasks[i].active = false;
                        if(tasks[i].handle != NULL) {
                            vTaskDelete(tasks[i].handle);
                            tasks[i].handle = NULL;
                        }
                    }
                }
            }
        }
    }
}
