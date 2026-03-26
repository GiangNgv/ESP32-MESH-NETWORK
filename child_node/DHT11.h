#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

// DHT11 sensor type (only DHT11 supported)
#define DHT11_TYPE    11

// DHT11 timing constants
#define DHT11_START_SIGNAL_MS     20
#define DHT11_START_SIGNAL_US     30
#define DHT11_RESPONSE_TIMEOUT_US 100
#define DHT11_DATA_TIMEOUT_US     80
#define DHT11_READ_INTERVAL_MS    2000

// DHT11 error codes
typedef enum {
    DHT11_OK = 0,
    DHT11_ERROR_TIMEOUT,
    DHT11_ERROR_CHECKSUM,
    DHT11_ERROR_NO_RESPONSE,
    DHT11_ERROR_INVALID_PARAM
} dht11_error_t;

// DHT11 configuration structure
typedef struct {
    gpio_num_t pin;
    uint32_t last_read_time;
    uint32_t read_interval_ms;
} dht11_config_t;

// DHT11 data structure
typedef struct {
    float temperature;
    float humidity;
    uint8_t raw_data[5];
    dht11_error_t last_error;
    bool valid;
} dht11_data_t;

// Function prototypes
dht11_error_t dht11_init(dht11_config_t *config);
dht11_error_t dht11_read(dht11_config_t *config, dht11_data_t *data);
dht11_error_t dht11_read_raw(gpio_num_t pin, uint8_t *raw_data);
bool dht11_is_ready(dht11_config_t *config);
const char* dht11_error_to_string(dht11_error_t error);

// Utility functions
float dht11_convert_c_to_f(float celsius);
float dht11_convert_f_to_c(float fahrenheit);
float dht11_compute_heat_index(float temperature, float humidity, bool fahrenheit);

#ifdef __cplusplus
}
#endif

#endif // DHT11_H