
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
    rollback = app_rollback; //rollback link
    configASSERT(feed_dog != NULL);
    uint8_t idx;

    //User Tasks
    idx = task_register("countdown_task", 500, true, 0);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Invalid Task.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&countdown, "countdown_task", 4096, (void *)(uintptr_t)idx, 3, &countdown_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(countdown_handle, idx);
    }

    idx = task_register("blinker_1_task", 100, false, 10);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Invalid Task.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&blinker_1, "blinker_1_task", 4096, (void *)(uintptr_t)idx, 2, &blinker_1_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(blinker_1_handle, idx);
    }

    idx = task_register("blinker_2_task", 1500, false, 1500);
    if(idx == 255)
    {
        ESP_LOGE(TAG, "Invalid Task.");
    }
    else {
        BaseType_t r = xTaskCreatePinnedToCore(&blinker_2, "blinker_2_task", 4096, (void *)(uintptr_t)idx, 2, &blinker_2_handle, 1);
        configASSERT(r == pdPASS);
        task_helper(blinker_2_handle, idx);
    }

    xTaskCreatePinnedToCore(&watchdog, "watchdog_task", 8192, NULL, 15, &watchdog_handle, 1);
    wdt_handle = &watchdog_handle;

    UBaseType_t highwatermark;
    highwatermark = uxTaskGetStackHighWaterMark(blinker_1_handle);
    ESP_LOGI(TAG, "Blinker Task 1 : min free stack = %u words", highwatermark);
    highwatermark = uxTaskGetStackHighWaterMark(blinker_2_handle);
    ESP_LOGI(TAG, "Blinker Task 2 : min free stack = %u words", highwatermark);
    highwatermark = uxTaskGetStackHighWaterMark(countdown_handle);
    ESP_LOGI(TAG, "Countdown Task : min free stack = %u words", highwatermark);
    highwatermark = uxTaskGetStackHighWaterMark(watchdog_handle);
    ESP_LOGI(TAG, "Watchdog Task : min free stack = %u words", highwatermark);
    highwatermark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "Main Task : min free stack = %u words", highwatermark);
    
    vTaskDelay(portMAX_DELAY);
}
