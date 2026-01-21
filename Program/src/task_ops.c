
#include "task_ops.h"

static const char *TAG = "TASK_OPS";

//Macros
#define LED_BUILTIN GPIO_NUM_2
#define TASK_INTERVAL 5000
#define BLINK_INTERVAL 250

void (*feed_dog)(uint8_t) = NULL; //Watchdog Feeder

//RTOS handles
SemaphoreHandle_t gpio_mutex;
TaskHandle_t blinker_1_handle;
TaskHandle_t blinker_2_handle;
TaskHandle_t countdown_handle;

void gpio_init(void) {
    gpio_config_t io_conf = { //GPIO Configuration Structure
        .pin_bit_mask = (1ULL << LED_BUILTIN), //GPIO pin mask
        .mode = GPIO_MODE_OUTPUT, //Pinmode (I/O)
        .pull_up_en = GPIO_PULLUP_DISABLE, //PullUp Resistor
        .pull_down_en = GPIO_PULLDOWN_DISABLE, //PullDown Resistor
        .intr_type = GPIO_INTR_DISABLE,    //Interrupt Options (GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE, GPIO_INTR_ANYEDGE, GPIO_INTR_LOW_LEVEL, GPIO_INTR_HIGH_LEVEL) 
    };
    gpio_config(&io_conf);
    
    gpio_mutex = xSemaphoreCreateMutex(); //Builtin LED Lock
    configASSERT(gpio_mutex != NULL);
}

void countdown(void *pvParameters)
{
    uint8_t tid = (uint8_t)(uintptr_t)pvParameters;
    uint64_t start_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Timer started at: %llu us", start_time);

    while(1) {
        uint64_t elapsed = esp_timer_get_time() - start_time;
        ESP_LOGI(TAG, "Current time: %llu s", (elapsed/1000000ULL));
        for(uint8_t i = 0; i < 40; i++)
        {
            if(feed_dog) {
                feed_dog(tid);
            }
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

void blinker_1(void *pvParameters) {
    uint8_t tid = (uint8_t)(uintptr_t)pvParameters;
    uint8_t blink_count = 3;
    
    while(1) {
        if (xSemaphoreTake(gpio_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
        {
            for(uint8_t i = 0; i < (TASK_INTERVAL / 500); i++)
            {
                if(feed_dog) {
                    feed_dog(tid);
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            ESP_LOGI(TAG, "Blinker Task 1 Active.");
            for(uint8_t i = 0; i < blink_count; i++) {
                if(feed_dog) {
                    feed_dog(tid);
                }
                gpio_set_level(LED_BUILTIN, 1);
                vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
                gpio_set_level(LED_BUILTIN, 0);
                vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
            }
            xSemaphoreGive(gpio_mutex);
            if(feed_dog) {
                feed_dog(tid);
            }
            vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
        }
        if(feed_dog) {
            feed_dog(tid);
        }
    }
}

void blinker_2(void *pvParameters) {
    uint8_t tid = (uint8_t)(uintptr_t)pvParameters;
    uint8_t blink_count = 2;

    while(1) {
        if (xSemaphoreTake(gpio_mutex, pdMS_TO_TICKS(500)) == pdTRUE)
        {
            for(uint8_t i = 0; i < (TASK_INTERVAL / 500); i++)
            {
                if(feed_dog) {
                    feed_dog(tid);
                }
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            ESP_LOGI(TAG, "Blinker Task 2 Active.");
            for(uint8_t i = 0; i < blink_count; i++) {
                if(feed_dog) {
                    feed_dog(tid);
                }
                gpio_set_level(LED_BUILTIN, 1);
                vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
                gpio_set_level(LED_BUILTIN, 0);
                vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
            }
            xSemaphoreGive(gpio_mutex);
            if(feed_dog) {
                feed_dog(tid);
            }
            vTaskDelay(pdMS_TO_TICKS(BLINK_INTERVAL));
        }
        if(feed_dog) {
            feed_dog(tid);
        }
    }
}
