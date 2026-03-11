
#include "wdt_ops.h"

#define MAX_TASKS 64
#define MONITOR_INTERVAL 1000  //ms

typedef struct {
    TaskHandle_t task_handle;
    const char *name;
    TickType_t last_checkin_tick; //Last Feed Timestamp
    TickType_t timeout_ticks; //Timeout Interval
    TickType_t rollback_ticks; //Wait Interval for full reboot.
    bool critical; //Rollback = true; Restart = false
    bool active; //Status
} wdt_task_t;

static const char *TAG = "WDT_OPS";
static wdt_task_t tasks[MAX_TASKS];
static uint8_t task_count = 0;
void (*rollback)(void) = NULL;

TaskHandle_t watchdog_handle;

//Register Targets
uint8_t task_register(const char *name, uint32_t timeout_tim, bool crit, uint32_t rollback_tim) {
    if (task_count < MAX_TASKS) {
        tasks[task_count].task_handle = NULL;
        tasks[task_count].name = name;
        tasks[task_count].timeout_ticks = pdMS_TO_TICKS(timeout_tim);
        tasks[task_count].last_checkin_tick = xTaskGetTickCount();
        tasks[task_count].critical = crit;
        tasks[task_count].active = true;
        tasks[task_count].rollback_ticks = pdMS_TO_TICKS(rollback_tim);

        task_count++;
        return task_count - 1; //Task ID
    }
    else {
        ESP_LOGE(TAG, "Max Tasks Reached");
    }
    return 255;
}

void task_helper(TaskHandle_t task_handle, uint8_t id)
{
    if(id < task_count)
    {
        tasks[id].task_handle = task_handle;
    }
}

//Timestamp update
void wdt_feed(uint8_t task_id) {
    if (task_id < task_count) {
        tasks[task_id].last_checkin_tick = xTaskGetTickCount();
    }
    else {
        ESP_LOGE(TAG, "Task ID exceeds max permitted value");
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
                        if (rollback) {
                            rollback();
                        }
                    }
                    else {
                        ESP_LOGE(TAG, "Non-Critical Task Fail: Terminating Task %s", tasks[i].name);
                        tasks[i].active = false;

                        if(tasks[i].task_handle != NULL) {
                            xTaskNotifyGive(tasks[i].task_handle);
                            if(ulTaskNotifyTake(pdTRUE, tasks[i].rollback_ticks) == 0) {
                                ESP_LOGE(TAG, "Task Termination Fail: Rollback");
                                if(rollback) {
                                    rollback();
                                }
                            }
                            vTaskDelete(tasks[i].task_handle);
                            tasks[i].task_handle = NULL;
                        }
                    }
                }
            }
        }
    }
}
