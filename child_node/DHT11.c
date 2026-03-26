#include "DHT11.h"
#include <string.h>
#include <math.h>

static const char *TAG = "DHT11";

static uint32_t dht11_get_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int dht11_wait_for_level(gpio_num_t pin, int level, uint32_t timeout_us) {
    uint32_t start_time = esp_timer_get_time();
    uint32_t elapsed_time = 0;
    
    while (gpio_get_level(pin) != level) {
        elapsed_time = esp_timer_get_time() - start_time;
        if (elapsed_time > timeout_us) {
            return -1; // Timeout
        }
        ets_delay_us(1);
    }
    return elapsed_time;
}

dht11_error_t dht11_init(dht11_config_t *config) {
    if (config == NULL) {
        return DHT11_ERROR_INVALID_PARAM;
    }
    
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << config->pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(ret));
        return DHT11_ERROR_INVALID_PARAM;
    }
    
    gpio_set_level(config->pin, 1);
    config->last_read_time = 0;
    
    if (config->read_interval_ms == 0) {
        config->read_interval_ms = DHT11_READ_INTERVAL_MS;
    }
    
    ESP_LOGI(TAG, "DHT11 initialized on GPIO %d", config->pin);
    return DHT11_OK;
}

bool dht11_is_ready(dht11_config_t *config) {
    if (config == NULL) {
        return false;
    }
    
    uint32_t current_time = dht11_get_time_ms();
    return (current_time - config->last_read_time) >= config->read_interval_ms;
}

// Send start signal to DHT11
static dht11_error_t dht11_send_start_signal(gpio_num_t pin) {
    // Set pin as output and pull low for 20ms
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(DHT11_START_SIGNAL_MS));
    
    // Pull high for 30us then switch to input
    gpio_set_level(pin, 1);
    ets_delay_us(DHT11_START_SIGNAL_US);
    
    // Configure as input with pull-up
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    
    return DHT11_OK;
}

// Wait for DHT11 response
static dht11_error_t dht11_wait_response(gpio_num_t pin) {
    // Wait for DHT11 to pull low (response signal)
    if (dht11_wait_for_level(pin, 0, DHT11_RESPONSE_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No response: timeout waiting for low signal");
        return DHT11_ERROR_NO_RESPONSE;
    }
    
    // Wait for DHT11 to pull high
    if (dht11_wait_for_level(pin, 1, DHT11_RESPONSE_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No response: timeout waiting for high signal");
        return DHT11_ERROR_NO_RESPONSE;
    }
    
    // Wait for DHT11 to pull low again (start of data)
    if (dht11_wait_for_level(pin, 0, DHT11_RESPONSE_TIMEOUT_US) < 0) {
        ESP_LOGW(TAG, "No response: timeout waiting for data start");
        return DHT11_ERROR_NO_RESPONSE;
    }
    
    return DHT11_OK;
}

// Read single bit from DHT11
static int dht11_read_bit(gpio_num_t pin) {
    // Wait for bit start (low to high transition)
    if (dht11_wait_for_level(pin, 1, 60) < 0) {
        return -1; // Timeout
    }
    
    // Measure high pulse duration
    int high_duration = dht11_wait_for_level(pin, 0, DHT11_DATA_TIMEOUT_US);
    //printf("High pulse duration: %d us\n", high_duration);
    if (high_duration < 0) {
        return -1; // Timeout
    }
    
    // Determine bit value based on high pulse duration
    // 0 bit: ~26-28us, 1 bit: ~70us
    return (high_duration > 40) ? 1 : 0;
}

// Read raw data from DHT11
dht11_error_t dht11_read_raw(gpio_num_t pin, uint8_t *raw_data) {
    if (raw_data == NULL) {
        return DHT11_ERROR_INVALID_PARAM;
    }
    
    // Clear data buffer
    memset(raw_data, 0, 5);
    
    // Send start signal
    dht11_error_t result = dht11_send_start_signal(pin);
    if (result != DHT11_OK) {
        return result;
    }
    
    // Wait for response
    result = dht11_wait_response(pin);
    if (result != DHT11_OK) {
        return result;
    }
    
    // Read 40 bits (5 bytes) of data
    for (int byte_idx = 0; byte_idx < 5; byte_idx++) {
        for (int bit_idx = 7; bit_idx >= 0; bit_idx--) {
            int bit = dht11_read_bit(pin);
            if (bit < 0) {
                ESP_LOGE(TAG, "Failed to read bit %d of byte %d", bit_idx, byte_idx);
                return DHT11_ERROR_TIMEOUT;
            }
            raw_data[byte_idx] |= (bit << bit_idx);
        }
    }
    
    ESP_LOGD(TAG, "Raw data: %02X %02X %02X %02X %02X", 
             raw_data[0], raw_data[1], raw_data[2], raw_data[3], raw_data[4]);
    
    return DHT11_OK;
}

// Validate checksum
static bool dht11_validate_checksum(uint8_t *raw_data) {
    uint8_t checksum = raw_data[0] + raw_data[1] + raw_data[2] + raw_data[3];
    return (checksum == raw_data[4]);
}

// Read temperature and humidity from DHT11
dht11_error_t dht11_read(dht11_config_t *config, dht11_data_t *data) {
    if (config == NULL || data == NULL) {
        return DHT11_ERROR_INVALID_PARAM;
    }
    
    // Check if enough time has passed since last read
    if (!dht11_is_ready(config)) {
        ESP_LOGW(TAG, "Read too soon, wait %d ms", 
                (int)(config->read_interval_ms - (dht11_get_time_ms() - config->last_read_time)));
        return DHT11_ERROR_TIMEOUT;
    }
    
    // Đưa dữ liệu trong biến data về 0
    memset(data, 0, sizeof(dht11_data_t));
    
    // Read raw data
    dht11_error_t result = dht11_read_raw(config->pin, data->raw_data);
    if (result != DHT11_OK) {
        data->last_error = result;
        data->valid = false;
        return result;
    }
    
    // Validate checksum
    if (!dht11_validate_checksum(data->raw_data)) {
        ESP_LOGE(TAG, "Checksum validation failed");
        data->last_error = DHT11_ERROR_CHECKSUM;
        data->valid = false;
        return DHT11_ERROR_CHECKSUM;
    }
    
    // Parse DHT11 data (integer values only)
    data->humidity = (float)data->raw_data[0];
    data->temperature = (float)data->raw_data[2];
    
    // Update last read time
    config->last_read_time = dht11_get_time_ms();
    data->last_error = DHT11_OK;
    data->valid = true;
    
    ESP_LOGI(TAG, "Temperature: %.1f°C, Humidity: %.1f%%", 
             data->temperature, data->humidity);
    
    return DHT11_OK;
}

// Convert error code to string
const char* dht11_error_to_string(dht11_error_t error) {
    switch (error) {
        case DHT11_OK:
            return "Success";
        case DHT11_ERROR_TIMEOUT:
            return "Timeout";
        case DHT11_ERROR_CHECKSUM:
            return "Checksum error";
        case DHT11_ERROR_NO_RESPONSE:
            return "No response from sensor";
        case DHT11_ERROR_INVALID_PARAM:
            return "Invalid parameter";
        default:
            return "Unknown error";
    }
}

// Utility function: Convert Celsius to Fahrenheit
float dht11_convert_c_to_f(float celsius) {
    return celsius * 1.8f + 32.0f;
}

// Utility function: Convert Fahrenheit to Celsius
float dht11_convert_f_to_c(float fahrenheit) {
    return (fahrenheit - 32.0f) / 1.8f;
}

// Utility function: Compute heat index
float dht11_compute_heat_index(float temperature, float humidity, bool fahrenheit) {
    float hi;
    
    if (!fahrenheit) {
        temperature = dht11_convert_c_to_f(temperature);
    }
    
    hi = 0.5f * (temperature + 61.0f + ((temperature - 68.0f) * 1.2f) + (humidity * 0.094f));
    
    if (hi > 79.0f) {
        hi = -42.379f + 2.04901523f * temperature + 10.14333127f * humidity +
             -0.22475541f * temperature * humidity +
             -0.00683783f * temperature * temperature +
             -0.05481717f * humidity * humidity +
             0.00122874f * temperature * temperature * humidity +
             0.00085282f * temperature * humidity * humidity +
             -0.00000199f * temperature * temperature * humidity * humidity;
        
        if ((humidity < 13.0f) && (temperature >= 80.0f) && (temperature <= 112.0f)) {
            hi -= ((13.0f - humidity) * 0.25f) * sqrtf((17.0f - fabsf(temperature - 95.0f)) * 0.05882f);
        } else if ((humidity > 85.0f) && (temperature >= 80.0f) && (temperature <= 87.0f)) {
            hi += ((humidity - 85.0f) * 0.1f) * ((87.0f - temperature) * 0.2f);
        }
    }
    
    return fahrenheit ? hi : dht11_convert_f_to_c(hi);
}