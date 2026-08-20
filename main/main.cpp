// Código feito por: | Coding done by:
// Leonardo Pereira

// TO KEEP IN MIND:
// Use pdTRUE and pdFALSE (returns BaseType_t) to align with FreeRTOS
// LOGI for info, LOGE for errors, LOGW for warnings
// Don't use mutex for ISR
// void *pvParameters necessary for functions in FreeRTOS
// NULL parameter for Task Watchdog Timer = current task
// No returns inside task functions
// portNUM_PROCESSORS = number of cores in the ESP32
// stack size parameter of xTaskCreate in ESP-IDF is measured in bytes, purely on FreeRTOS is actually words

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

// ESP_LOG tag
static const char *system_tag = "SYSTEM";

EventGroupHandle_t sensor_events;
const int kBitTaskLogic = (1 << 0);         // Bit 0 assigned to Task 2 (Sensor Logic)
const int kBitTaskTelemetry = (1 << 1);     // Bit 1 assigned to Task 3 (Sensor Logging)

float current_temperature = 0.0;
bool fault_sim = false;
SemaphoreHandle_t temperature_mutex;

//  Task 1: Sensor Reading
//  Strict 2s synchronization of safe writing to shared variable, with esp task watchdog to save
//  from possible system locking up. Uses event groups to signal other tasks.
void TaskSensorReading(void *pvParameters) {

    esp_task_wdt_add(NULL); 

    // Stores start time and sets frequency for proper 2s synchornization
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t wake_frequency = pdMS_TO_TICKS(2000);

    int fault_counter = 0;

    while(true) {
        // "Kicks the dog": resets timer so the watchdog doesn't trigger a restart
        esp_task_wdt_reset(); 

        // Fault sim after 10s
        fault_counter++;
        if (fault_counter == 5) {
            fault_sim = true; 
            ESP_LOGE(system_tag, "[FAULT] DHT11 disconnected or non responsive");
        }

        // Safe writing of current temperature
        if (xSemaphoreTake(temperature_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!fault_sim) {

                // Temperature sim
                current_temperature = 25.0f + (rand() % 70) / 10.0f; 
            }
            xSemaphoreGive(temperature_mutex);
        }
        
        // Allows the wakeup of task 2 and 3
        xEventGroupSetBits(sensor_events, kBitTaskLogic | kBitTaskTelemetry);

        vTaskDelayUntil(&last_wake_time, wake_frequency);
    }

}

//  Task 2: Sensor Logic
//  Event triggered reading of shared variable with a simple logic check (to be changed).
void TaskLogic(void *pvParameters) {

    float local_temperature = 0.0;

    while(true) {
        xEventGroupWaitBits(sensor_events, kBitTaskLogic, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(temperature_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            local_temperature = current_temperature;
            xSemaphoreGive(temperature_mutex);
            
            if (!fault_sim && local_temperature > 30.0) {
                ESP_LOGW(system_tag, "[ALERT] High temperature (%.1fC)", local_temperature);
            }
        }
    }

}

//  Task 3: Sensor Telemetry
//  Event triggered reading of shared variable with subsequent logging on the console.
void TaskTelemetry(void *pvParameters) {
    
    float local_temperature = 0.0;

    while(true) {
        xEventGroupWaitBits(sensor_events, kBitTaskTelemetry, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(temperature_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            local_temperature = current_temperature;
            xSemaphoreGive(temperature_mutex);

            if (!fault_sim) {
                ESP_LOGI(system_tag, "[LOG] Temperatura atual: %.1fC - Sistema OK", local_temperature);
            }
        }
    }

}

//  Entry point
//  Watchdog timer, mutex, event group and tasks initialization
extern "C" void app_main() {

    ESP_LOGI(system_tag, "[INFO] Starting system...");

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of cores to be monitored (00000011)
        .trigger_panic = true,                              // Triggers reset if lock up
    };
    // _reconfigure instead of _init because it is already initialized in current ESP-IDF
    esp_task_wdt_reconfigure(&twdt_config);

    temperature_mutex = xSemaphoreCreateMutex();
    sensor_events = xEventGroupCreate();
    if (temperature_mutex == NULL || sensor_events == NULL) {
        ESP_LOGE(system_tag, "[FAULT] Mutex creation unsuccessful. System aborted.");
        return;
    }

    // Parameters: function, name, stack size (bytes), parameter to be passed, priority, optional task handle
    xTaskCreate(TaskSensorReading, "TaskSensor", 2048, NULL, 5, NULL);
    xTaskCreate(TaskLogic, "TaskLogic", 2048, NULL, 4, NULL);
    xTaskCreate(TaskTelemetry,  "TaskTelemetry",   2048, NULL, 3, NULL);

}