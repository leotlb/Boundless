#pragma once                // Avoid duplicity
#include "driver/gpio.h"

class DHT11 {
private:
    gpio_num_t pin_;
    float temperature_;
    float humidity_;
    bool sensor_fail_;
public:
    DHT11(gpio_num_t pin_dht);

    bool ReadData();
    
    float GetTemperature();
    float GetHumidity();
    bool InFailState();
};