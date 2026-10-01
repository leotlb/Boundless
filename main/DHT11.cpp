// Código feito por: | Coding done by:
// Leonardo Pereira

#include "DHT11.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

// DHT11 Constructor
DHT11::DHT11(gpio_num_t pino_dht) {

    this->pin_ = pino_dht;
    this->temperature_ = 0.0;
    this->humidity_ = 0.0;
    this->sensor_fail_ = false;

}

// Returns the microseconds taken for the state to change to the one required with a timeout clause
int DHT11::WaitState(int required_state, int timeout_us) {

    int64_t initial_time = esp_timer_get_time();
    while (gpio_get_level(this->pin_) != required_state) {
        int64_t time_passed_us = esp_timer_get_time() - initial_time;
        if (time_passed_us > timeout_us) {
            return -1;  // Timeout
        }
    }
    return (int)(esp_timer_get_time() - initial_time);

}

// Handshakes the DHT11, converts the interval of the pull ups to data and writes it to the relevant class.
// There's no need for orquestration since SensorReading() is the only task with access to this class and it is 
// either copying the data to the context struct, waiting for the next checkpoint or waiting for the result of ReadData()
bool DHT11::ReadData() {

    uint8_t data[5] = {0, 0, 0, 0, 0};

    // ESP32 handshake
    gpio_set_direction(this->pin_, GPIO_MODE_OUTPUT);
    gpio_set_level(this->pin_, 0);
    vTaskDelay(pdMS_TO_TICKS(20)); // Safety margin, according to spec happens at 18ms
    gpio_set_level(this->pin_, 1);
    esp_rom_delay_us(30);          // Middle of the 20-40us window spec
    gpio_set_direction(this->pin_, GPIO_MODE_INPUT);

    // DHT11 handshake
    if (WaitState(0, 200) == -1) { return false; }  // Timeout safety margin, according to spec happens at 80us
    if (WaitState(1, 200) == -1) { return false; }  // Timeout safety margin, according to spec happens at 80us
    if (WaitState(0, 200) == -1) { return false; }  // Timeout safety margin, ideally happens instantaneously after previous WaitState

    // Bit banging
    for (int i = 0; i < 40; i++) {

        // Waits out the initial pulldown preceding every bit
        if (WaitState(1, 60) == -1) return false;   // Safety margin, according to spec happens at 50us
        
        // Times the high state
        int high_time_us = WaitState(0, 90);
        if (high_time_us == -1) return false;

        // Defines which byte the data gets allocated
        int byte_index = i / 8;
        data[byte_index] <<= 1;
        
        // According to spec 0 is a pull up of max 28us so anything higher is considered 1 (according to spec, 1 is 70us)
        if (high_time_us > 40) {
            data[byte_index] |= 1; // The 1 works as a mask to flip the last shifted 0 with bitwise or
        }
    }

    // Checksum verification
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        this->sensor_fail_ = true;
        return false;
    }

    // Actual data update
    this->humidity_ = (float)data[0] + ((float)data[1] / 10.0f);
    this->temperature_ = (float)data[2] + ((float)data[3] / 10.0f);
    this->sensor_fail_ = false;
    
    printf("RAW: %d %d %d %d | Check: %d\n", data[0], data[1], data[2], data[3], data[4]);
    return true;

}


float DHT11::GetTemperature() { return this->temperature_; }
float DHT11::GetHumidity() { return this->humidity_; }
bool DHT11::InFailState() { return this->sensor_fail_; }