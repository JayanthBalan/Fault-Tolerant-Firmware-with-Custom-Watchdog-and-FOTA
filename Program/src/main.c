
#include "config.h"
#include "ota_ops.h"
#include "task_ops.h"
#include "wdt_ops.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ///Background Initializations
    ota_ops_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_init();
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Initialization Complete.");

    feed_dog = wdt_feed; //feed link
    configASSERT(feed_dog != NULL);
    uint8_t idx;

    //User Tasks
    idx = task_register("countdown_task", 500, true);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Max Tasks Reached.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&countdown, "countdown_task", 2048, (void *)(uintptr_t)idx, 3, &countdown_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(countdown_handle, idx);
    }

    idx = task_register("blinker_1_task", 1000, false);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Max Tasks Reached.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&blinker_1, "blinker_1_task", 4096, (void *)(uintptr_t)idx, 2, &blinker_1_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(blinker_1_handle, idx);
    }

    idx = task_register("blinker_2_task", 1500, false);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Max Tasks Reached.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&blinker_2, "blinker_2_task", 4096, (void *)(uintptr_t)idx, 2, &blinker_2_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(blinker_2_handle, idx);
    }

    xTaskCreatePinnedToCore(&watchdog, "watchdog_task", 4096, NULL, 15, &watchdog_handle, 1);
    
    vTaskDelay(portMAX_DELAY);
}
