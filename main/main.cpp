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

#include "DHT11.h"

DHT11 sensor_DHT(GPIO_NUM_4);


static const char *system_tag = "SYSTEM";   // ESP_LOG tag
const int kBitTaskLogic = (1 << 0);         // Bit 0 assigned to Task 2 (Sensor Logic)
const int kBitTaskTelemetry = (1 << 1);     // Bit 1 assigned to Task 3 (Sensor Logging)

struct SystemContext {
    SemaphoreHandle_t temperature_mutex;
    EventGroupHandle_t sensor_events;
    
    // Shared data (previously global)
    float current_temperature;
    bool fault_sim;
};

//  Task 1: Sensor Reading
//  Strict 2s synchronization of safe writing of shared variable, with esp task watchdog to save
//  from possible system locking up. Uses event groups to signal other tasks.
void TaskSensorReading(void *pvParameters) {

    //esp_task_wdt_add(NULL);
    SystemContext* ctx = (SystemContext*)pvParameters;

    // Stores start time and sets frequency for proper 2s synchornization
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t wake_frequency = pdMS_TO_TICKS(2000);

    while(true) {

        if (xSemaphoreTake(ctx->temperature_mutex, portMAX_DELAY) == pdTRUE) {
            ctx->fault_sim = sensor_DHT.InFailState();
            
            if ( sensor_DHT.ReadData() ) {
                ctx->current_temperature = sensor_DHT.GetTemperature();
            }
            
            xSemaphoreGive(ctx->temperature_mutex);
        }
        
        // Allows the wakeup of task 2 and 3
        xEventGroupSetBits(ctx->sensor_events, kBitTaskLogic | kBitTaskTelemetry);

        vTaskDelayUntil(&last_wake_time, wake_frequency);
    }

}

//  Task 2: Sensor Logic
//  Event triggered reading of shared variable with a simple logic check (to be changed).
void TaskLogic(void *pvParameters) {

    SystemContext* ctx = (SystemContext*)pvParameters;

    float local_temperature = 0.0;

    while(true) {
        xEventGroupWaitBits(ctx->sensor_events, kBitTaskLogic, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(ctx->temperature_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            local_temperature = ctx->current_temperature;
            xSemaphoreGive(ctx->temperature_mutex);
            
            if (!ctx->fault_sim && local_temperature > 30.0) {
                ESP_LOGW(system_tag, "[ALERT] High temperature (%.1fC)", local_temperature);
            }
        }
    }

}

//  Task 3: Sensor Telemetry
//  Event triggered reading of shared variable with subsequent logging on the console.
void TaskTelemetry(void *pvParameters) {

    SystemContext* ctx = (SystemContext*)pvParameters;
    
    float local_temperature = 0.0;

    while(true) {
        xEventGroupWaitBits(ctx->sensor_events, kBitTaskTelemetry, pdTRUE, pdFALSE, portMAX_DELAY);

        if (xSemaphoreTake(ctx->temperature_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            local_temperature = ctx->current_temperature;
            xSemaphoreGive(ctx->temperature_mutex);

            if (!ctx->fault_sim) {
                ESP_LOGI(system_tag, "[LOG] Current temperature: %.1fC - System OK", local_temperature);
            }
        }
    }

}

//  Entry point
//  Creation of the context for protected system variables; initialization for watchdog, mutex, event group and tasks
extern "C" void app_main() {

    ESP_LOGI(system_tag, "[INFO] Starting system...");

    static SystemContext context;
    context.temperature_mutex = xSemaphoreCreateMutex();
    context.sensor_events = xEventGroupCreate();
    context.current_temperature = 0.0f;
    context.fault_sim = false;

    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = 5000,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,    // Bitmask of cores to have their idle tasks monitored (00000011)
        .trigger_panic = true,                              // Triggers reset if lock up
    };
    // _reconfigure instead of _init because it is already initialized by default in current ESP-IDF
    esp_task_wdt_reconfigure(&twdt_config);

    context.temperature_mutex = xSemaphoreCreateMutex();
    context.sensor_events = xEventGroupCreate();
    if (context.temperature_mutex == NULL || context.sensor_events == NULL) {
        ESP_LOGE(system_tag, "[FAULT] Mutex creation unsuccessful. System aborted.");
        return;
    }

    // Parameters: function, name, stack size (bytes), parameter to be passed, priority, optional task handle
    xTaskCreate(TaskSensorReading, "TaskSensor", 2048, &context, 5, NULL);
    xTaskCreate(TaskLogic, "TaskLogic", 2048, &context, 4, NULL);
    xTaskCreate(TaskTelemetry,  "TaskTelemetry",   2048, &context, 3, NULL);

}