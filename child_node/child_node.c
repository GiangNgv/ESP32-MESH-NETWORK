#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mesh.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "DHT11.h"  

#define CONFIG_MESH_CHANNEL 6
#define CONFIG_MESH_ROUTER_SSID "Gavin"
#define CONFIG_MESH_ROUTER_PASSWD "3.141592654"
#define CONFIG_MESH_AP_CONNECTIONS 4
#define CONFIG_MESH_AP_PASSWD "mesh_password"
#define DHT11_GPIO GPIO_NUM_4  // GPIO pin for DHT11 sensor

uint8_t MESH_ID[6] = { 0x7C, 0xDF, 0xA1, 0x30, 0xAA, 0xBB };

// Biến global để kiểm soát task
static bool is_connected = false;
static bool task_should_stop = false;
dht11_t dht11_sensor;  // Biến để lưu thông tin cảm biến DHT11
TaskHandle_t mesh_task_handle = NULL;

// Task để đọc cảm biến DHT11 và gửi dữ liệu định kỳ tới root (phiên bản đơn giản)
void mesh_tx_task(void *arg)
{
    esp_err_t err;
    mesh_data_t data;
    int send_count = 0;
    int read_count = 0;
    TickType_t last_send_time = 0;
    TickType_t last_read_time = 0;
    
    ESP_LOGI("MESH_TASK", "Mesh sensor and TX task started");
    
    while (!task_should_stop) {
        // Chỉ hoạt động khi đã kết nối
        if (is_connected) {
            TickType_t current_time = xTaskGetTickCount();
            
            // Đọc cảm biến DHT11 mỗi 5 giây
            if (current_time - last_read_time >= pdMS_TO_TICKS(5000)) {
                ESP_LOGI("MESH_TASK", "Reading DHT11 sensor...");
                
                int dht_result = dht11_read(&dht11_sensor, 5); 
                
                if (dht_result == 0) {
                    read_count++;
                    ESP_LOGI("MESH_TASK", "Sensor read #%d: T=%.1f°C, H=%.1f%%", 
                             read_count, dht11_sensor.temperature, dht11_sensor.humidity);
                } else {
                    ESP_LOGW("MESH_TASK", "Sensor read failed with code: %d", dht_result);
                }
                
                last_read_time = current_time;
            }
            
            // Gửi dữ liệu mỗi 10 giây
            if (current_time - last_send_time >= pdMS_TO_TICKS(10000)) {
                if (dht11_sensor.temperature != 0 || dht11_sensor.humidity != 0) {
                    // Tạo chuỗi JSON chứa dữ liệu cảm biến
                    char json_data[128];
                    snprintf(json_data, sizeof(json_data), 
                             "{\"Temp\":%.1f,\"humid\":%.1f}",
                             dht11_sensor.temperature, 
                             dht11_sensor.humidity);
                    
                    // Chuẩn bị dữ liệu để gửi
                    data.data = (uint8_t *)json_data;
                    data.size = strlen(json_data);
                    data.proto = MESH_PROTO_JSON;
                    data.tos = MESH_TOS_P2P;
                    
                    // Gửi dữ liệu đến root node
                    err = esp_mesh_send(NULL, &data, MESH_DATA_P2P, NULL, 0);
                    
                    if (err == ESP_OK) {
                        send_count++;
                        ESP_LOGI("MESH_TASK", "Data sent #%d: T=%.1f°C, H=%.1f%%", 
                                 send_count, dht11_sensor.temperature, dht11_sensor.humidity);
                    } else {
                        ESP_LOGE("MESH_TASK", "Failed to send data: %s", esp_err_to_name(err));
                    }
                    
                    last_send_time = current_time;
                } else {
                    ESP_LOGD("MESH_TASK", "No valid sensor data to send");
                    last_send_time = current_time;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            ESP_LOGW("MESH_TASK", "Not connected to parent, waiting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
    }
    
    ESP_LOGI("MESH_TASK", "Mesh sensor and TX task stopped");
    mesh_task_handle = NULL;
    vTaskDelete(NULL);
}

// Event handler để xử lý các sự kiện mesh
void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id) {
        case MESH_EVENT_STARTED:
            ESP_LOGI("CHILD", "Mesh started");
            break;
            
        case MESH_EVENT_PARENT_CONNECTED:
            ESP_LOGI("CHILD", "Connected to parent");
            is_connected = true;
            
            // Khởi động task mesh nếu chưa được tạo
            if (mesh_task_handle == NULL) {
                xTaskCreate(mesh_tx_task, "mesh_task", 8192, NULL, 5, &mesh_task_handle);
                ESP_LOGI("CHILD", "Mesh task created with larger stack");
            }
            break;
            
        case MESH_EVENT_PARENT_DISCONNECTED:
            ESP_LOGI("CHILD", "Disconnected from parent");
            is_connected = false;
            
            // Signal task to stop gracefully
            task_should_stop = true;
            
            // Wait for mesh task to self terminate
            if (mesh_task_handle != NULL) {
                ESP_LOGI("CHILD", "Waiting for Mesh Task to stop...");
                for (int i = 0; i < 50 && mesh_task_handle != NULL; i++) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (mesh_task_handle != NULL) {
                    ESP_LOGW("CHILD", "Force deleting Mesh Task");
                    vTaskDelete(mesh_task_handle);
                    mesh_task_handle = NULL;
                }
            }
            
            // Reset flag after stopping task
            task_should_stop = false;
            ESP_LOGI("CHILD", "Mesh task stopped");
            break;
            
        case MESH_EVENT_NO_PARENT_FOUND:
            ESP_LOGI("CHILD", "No parent found, scanning...");
            break;
            
        default:
            ESP_LOGD("CHILD", "Unhandled mesh event: %ld", (long)event_id);
            break;
    }
}

void app_main(void)
{
    // Khởi tạo hệ thống
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Init WiFi stack
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Bật self-organized cho child node
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(true, false));

    // Đăng ký event handler để lắng nghe sự kiện mesh
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));

    // Init mesh
    ESP_ERROR_CHECK(esp_mesh_init());

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    cfg.channel = CONFIG_MESH_CHANNEL;

    //Cấu hình router cho child node
    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD, strlen(CONFIG_MESH_ROUTER_PASSWD));

    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD, strlen(CONFIG_MESH_AP_PASSWD));
    
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    
    ESP_ERROR_CHECK(esp_mesh_start());
    
    // Initialize global sensor struct
    memset(&dht11_sensor, 0, sizeof(dht11_t));
    dht11_sensor.dht11_pin = DHT11_GPIO;
    
    // Cấu hình GPIO cho DHT11 với pull-up resistor
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT11_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    ESP_LOGI("MAIN", "GPIO %d configured with pull-up", DHT11_GPIO);
    
    ESP_LOGI("MAIN", "DHT11 sensor configured on GPIO %d", DHT11_GPIO);
    
    ESP_LOGI("MAIN", "Child Node initialization completed");
    
    // Add delay to let mesh stabilize first
    vTaskDelay(pdMS_TO_TICKS(3000));

}