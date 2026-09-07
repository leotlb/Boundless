#include "DHT11.h"
#include "esp_random.h"         // Temporary
#include "freertos/FreeRTOS.h"

// DHT11 Constructor
DHT11::DHT11(gpio_num_t pino_dht) {
    this->pin_ = pino_dht;
    this->temperature_ = 0.0;
    this->humidity_ = 0.0;
    this->sensor_fail_ = false;
    
    // GPIO TBD...
}

// Generates temperature and humidity (temporary) with fault sim (temporary)
bool DHT11::ReadData() {
    
    static int read_counter = 0;
    read_counter++;
    if (read_counter > 5) {
        sensor_fail_ = true;
        return false;
    }

    uint32_t random_value = esp_random();
    this->temperature_ = 25.0f + (random_value % 70) / 10.0f;
    this->humidity_ = 50.0f + (random_value % 20);
    
    return true;
}

float DHT11::GetTemperature() {
    return this->temperature_;
}

float DHT11::GetHumidity() {
    return this->humidity_;
}

bool DHT11::InFailState() {
    return this->sensor_fail_;
}