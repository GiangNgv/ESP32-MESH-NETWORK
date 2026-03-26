#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stdbool.h"
/*---------------FreeRTOS---------------*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
/*-----------WIFI, MQTT, MESH-----------*/
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_mesh.h"
#include "esp_netif.h"
#include "nvs_flash.h"
/*---------------Debug-----------------*/
#include "esp_event.h"
#include "esp_log.h"
/*---------------JSON-----------------*/
#include "cJSON.h"

// Forward declarations
void send_mesh_info_to_mqtt(const char* message);

#define CONFIG_MESH_CHANNEL 6
#define CONFIG_MESH_ROUTER_SSID "Gavin"
#define CONFIG_MESH_ROUTER_PASSWD "3.141592654"
#define CONFIG_MESH_AP_CONNECTIONS 4
#define CONFIG_MESH_AP_PASSWD "mesh_password"
// MQTT Configuration
#define BROKER_URI "mqtts://39f106d8a24641eb9a9024772d67216b.s1.eu.hivemq.cloud:8883"
#define BROKER_USERNAME "Gavin123"
#define BROKER_PASSWORD "Gavin123"
#define SUB_TOPIC "node_select"
#define PUB_TOPIC "sensor_data"
#define MESH_TOPIC "mesh_manage"

uint8_t MESH_ID[6] = { 0x7C, 0xDF, 0xA1, 0x30, 0xAA, 0xBB };

// Global variables
esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static bool mqtt_ever_connected = false; //Biến trạng thái kết nối MQTT
static char selected_node_mac[18] = ""; // MAC address của node được chọn (format: "XX:XX:XX:XX:XX:XX")
static bool wifi_connected = false;  // Biến để theo dõi trạng thái WiFi

/*-------------------------------------------Handlers-------------------------------------------*/
// MQTT Event Handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "MQTT_EVENT_CONNECTED - SUCCESS!");
            
            // Cập nhật trạng thái kết nối
            mqtt_connected = true;
            mqtt_ever_connected = true;
            mqtt_client = client; // Lưu handle để sử dụng sau này

            // Đăng ký nhận topic sau khi kết nối thành công
            esp_mqtt_client_subscribe(client, SUB_TOPIC, 1);
            
            esp_mqtt_client_publish(client, "mesh_manage", "ESP32 connected!", 0, 1, 0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW("MQTT", "MQTT_EVENT_DISCONNECTED");
            mqtt_connected = false;
            
            // Chỉ reconnect nếu đã từng kết nối thành công (tức là bị mất kết nối thật)
            if (mqtt_ever_connected) {
                ESP_LOGI("MQTT", "Connection lost - allowing reconnection...");
                // Để auto-reconnect tự xử lý, không cần manual reconnect
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI("MQTT", "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI("MQTT", "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI("MQTT", "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT", "MQTT_EVENT_DATA RECEIVED");
            printf("TOPIC: %.*s\r\n", event->topic_len, event->topic);
            printf("DATA : %.*s\r\n", event->data_len, event->data);
            
            // Xử lý topic node_select
            if (strncmp(event->topic, SUB_TOPIC, event->topic_len) == 0) {
                // Parse MAC address từ message nhận được
                char node_select_data[32];
                strncpy(node_select_data, event->data, event->data_len);
                node_select_data[event->data_len] = '\0';
                
                // Kiểm tra format MAC address (XX:XX:XX:XX:XX:XX)
                if (strlen(node_select_data) == 17 && 
                    node_select_data[2] == ':' && node_select_data[5] == ':' && 
                    node_select_data[8] == ':' && node_select_data[11] == ':' && 
                    node_select_data[14] == ':') {
                    strcpy(selected_node_mac, node_select_data);
                    ESP_LOGI("MQTT", "Node selection updated to MAC: %s", selected_node_mac);
                } else {
                    ESP_LOGW("MQTT", "Invalid MAC address format received: %s", node_select_data);
                }
            }
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE("MQTT", "MQTT_EVENT_ERROR");
            break;

        default:
            ESP_LOGW("MQTT", "Other event id:%d", event->event_id);
            break;
    }
}

//Wifi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI("WIFI", "WIFI_EVENT_STA_START");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI("WIFI", "WIFI_EVENT_STA_CONNECTED");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW("WIFI", "WIFI_EVENT_STA_DISCONNECTED");
            wifi_connected = false; // Cập nhật trạng thái WiFi
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI("WIFI", "WIFI_EVENT_AP_START");
            break;
        default:
            break;
    }
}

//Ip event handler
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI("IP_EVENT", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true; // Cập nhật trạng thái WiFi
    } else if (event_id == IP_EVENT_STA_LOST_IP) {
        ESP_LOGW("IP_EVENT", "Lost IP");
        wifi_connected = false; // Cập nhật trạng thái WiFi
    }
}

// Hàm kiểm tra xem có nên publish dữ liệu của node này không
bool should_publish_node(mesh_addr_t *addr) {
    // Tạo chuỗi MAC address từ addr
    char node_mac[18];
    snprintf(node_mac, sizeof(node_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->addr[0], addr->addr[1], addr->addr[2],
             addr->addr[3], addr->addr[4], addr->addr[5]);
    
    // So sánh với MAC address được chọn
    return (strlen(selected_node_mac) > 0 && strcmp(node_mac, selected_node_mac) == 0);
}

// Hàm gửi thông báo mesh management lên MQTT
void send_mesh_info_to_mqtt(const char* message) {
    if (mqtt_connected && mqtt_client != NULL) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, MESH_TOPIC, message, 0, 1, 0);
        ESP_LOGI("MQTT", "Published mesh info, msg_id: %d", msg_id);
    } else {
        ESP_LOGW("MQTT", "Cannot send mesh info - MQTT not connected");
    }
}

// Mesh event handler
void mesh_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    mesh_event_child_connected_t *child_connected = NULL;
    char mesh_message[256];
    
    switch (event_id) {
        case MESH_EVENT_CHILD_CONNECTED:
            child_connected = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI("ROOT", "Nut %02x:%02x:%02x:%02x:%02x:%02x da ket noi voi root node", 
                     child_connected->mac[0], child_connected->mac[1], child_connected->mac[2], 
                     child_connected->mac[3], child_connected->mac[4], child_connected->mac[5]);
            
            // Tạo message JSON chứa thông tin nút con mới kết nối (chỉ MAC address)
            snprintf(mesh_message, sizeof(mesh_message), 
                    "Child_connected, node_mac: %02X:%02X:%02X:%02X:%02X:%02X", 
                    child_connected->mac[0], child_connected->mac[1], child_connected->mac[2], 
                    child_connected->mac[3], child_connected->mac[4], child_connected->mac[5]);
            
            // Gửi thông tin lên MQTT topic mesh_management
            send_mesh_info_to_mqtt(mesh_message);
            break;
            
        case MESH_EVENT_CHILD_DISCONNECTED:
            child_connected = (mesh_event_child_connected_t *)event_data;
            ESP_LOGI("ROOT", "Nut %02x:%02x:%02x:%02x:%02x:%02x da mat ket noi voi root node", 
                     child_connected->mac[0], child_connected->mac[1], child_connected->mac[2], 
                     child_connected->mac[3], child_connected->mac[4], child_connected->mac[5]);
            
            // Tạo message JSON cho sự kiện disconnect (chỉ MAC address)
            snprintf(mesh_message, sizeof(mesh_message), 
                    "Child_disconnected, node_mac: %02X:%02X:%02X:%02X:%02X:%02X", 
                    child_connected->mac[0], child_connected->mac[1], child_connected->mac[2], 
                    child_connected->mac[3], child_connected->mac[4], child_connected->mac[5]);
            
            // Gửi thông tin lên MQTT topic mesh_management
            send_mesh_info_to_mqtt(mesh_message);
            break;
            
        default:
            break;
    }
}

// Hàm gửi dữ liệu lên MQTT
void send_to_mqtt(const char* message) {
    if (mqtt_connected && mqtt_client != NULL) {
        int msg_id = esp_mqtt_client_publish(mqtt_client, PUB_TOPIC, message, 0, 1, 0);
        ESP_LOGI("MQTT", "Published sensor data, msg_id: %d", msg_id);
    } else {
        ESP_LOGW("MQTT", "Cannot send data - MQTT not connected");
    }
}

// Task để nhận dữ liệu từ mesh
void mesh_rx_task(void *arg)
{
    mesh_data_t data;
    mesh_addr_t from;
    int flag = 0;
    char message[256];
    
    data.data = malloc(1024);
    data.size = 1024;
    
    while (true) {
        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err == ESP_OK && data.size > 0) {           
            data.data[data.size] = '\0';
            ESP_LOGI("ROOT", "Received from %02X:%02X:%02X:%02X:%02X:%02X: %s", 
                     from.addr[0], from.addr[1], from.addr[2], 
                     from.addr[3], from.addr[4], from.addr[5], 
                     (char*)data.data);
            
            // Parse JSON data từ nút con
            cJSON *json = cJSON_Parse((char*)data.data);
            if (json != NULL) {
                cJSON *temp_json = cJSON_GetObjectItem(json, "Temp");
                cJSON *humid_json = cJSON_GetObjectItem(json, "Humid");
                
                if (cJSON_IsNumber(temp_json) && cJSON_IsNumber(humid_json)) {
                    double temperature = temp_json->valuedouble;
                    double humidity = humid_json->valuedouble;
                    
                    // Format data message với node MAC address 
                    snprintf(message, sizeof(message), 
                            "node_mac: %02X:%02X:%02X:%02X:%02X:%02X. temperature: %.1f, humidity: %.1f", 
                            from.addr[0], from.addr[1], from.addr[2], 
                            from.addr[3], from.addr[4], from.addr[5],
                            temperature, humidity);
                    
                    ESP_LOGI("ROOT", "Parsed data - MAC: %02X:%02X:%02X:%02X:%02X:%02X, Temp: %.1f°C, Humidity: %.1f%%", 
                             from.addr[0], from.addr[1], from.addr[2], 
                             from.addr[3], from.addr[4], from.addr[5], temperature, humidity);
                } else {
                    // JSON không hợp lệ hoặc thiếu trường
                    snprintf(message, sizeof(message), 
                            "{\"node_mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"error\":\"Invalid JSON data\"}", 
                            from.addr[0], from.addr[1], from.addr[2], 
                            from.addr[3], from.addr[4], from.addr[5]);
                    ESP_LOGW("ROOT", "Invalid JSON data from MAC: %02X:%02X:%02X:%02X:%02X:%02X", 
                             from.addr[0], from.addr[1], from.addr[2], 
                             from.addr[3], from.addr[4], from.addr[5]);
                }
                cJSON_Delete(json);
            } else {
                // JSON parse failed - tạo error message
                snprintf(message, sizeof(message), 
                        "{\"node_mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"error\":\"JSON_PARSE_FAILED\",\"raw_data\":\"%s\"}", 
                        from.addr[0], from.addr[1], from.addr[2], 
                        from.addr[3], from.addr[4], from.addr[5], 
                        (char*)data.data);
                ESP_LOGW("ROOT", "Failed to parse JSON from MAC: %02X:%02X:%02X:%02X:%02X:%02X: %s", 
                         from.addr[0], from.addr[1], from.addr[2], 
                         from.addr[3], from.addr[4], from.addr[5], (char*)data.data);
            }

            if (should_publish_node(&from)) {
                ESP_LOGI("ROOT", "Sending data for selected node MAC: %s", selected_node_mac);
                send_to_mqtt(message);
            } else {
                char current_mac[18];
                snprintf(current_mac, sizeof(current_mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         from.addr[0], from.addr[1], from.addr[2], 
                         from.addr[3], from.addr[4], from.addr[5]);
                ESP_LOGD("ROOT", "Data from MAC %s ignored (selected: %s)", current_mac, selected_node_mac);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay nhỏ để không block CPU
    }
    
    free(data.data);
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Khởi tạo hệ thống
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    //Đăng ký event handler cho WiFi và IP
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    // Tạo netif cho WiFi station
    esp_netif_create_default_wifi_sta();

    //Init WiFi
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    //Cấu hình Wi-Fi để root tự kết nối router
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_MESH_ROUTER_SSID,
            .password = CONFIG_MESH_ROUTER_PASSWD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

   // Đợi WiFi kết nối thành công rồi bắt đầu kết nối MQTT
    ESP_LOGI("WIFI", "Waiting for WiFi connection...");
    int retry_count = 0;
    int max_retries = 30; // 30 giây timeout
    while (wifi_connected != 1 && retry_count < max_retries) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay 1 giây
        retry_count++;
        ESP_LOGI("WIFI", "Still waiting for WiFi... (%d/%d)", retry_count, max_retries);
    }
    if (wifi_connected) {
        ESP_LOGI("WIFI", "WiFi connected successfully!");

        // Đợi thêm một chút để đảm bảo DNS hoạt động
        vTaskDelay(pdMS_TO_TICKS(5000));  // Tăng delay lên 5 giây
        
        // Tạo client ID duy nhất từ MAC address
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        char client_id[32];
        snprintf(client_id, sizeof(client_id), "ESP32_%02X%02X%02X%02X%02X%02X", 
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        ESP_LOGI("MQTT", "Starting MQTT client with broker: %s", BROKER_URI);
        
        // Tạo MQTT config
        esp_mqtt_client_config_t ssl_mqtt_config = {
            .broker.address.uri = BROKER_URI,
            .broker.verification.use_global_ca_store = false,
            .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
            .broker.verification.skip_cert_common_name_check = false,
            .credentials.client_id = client_id,  
            .credentials.username = BROKER_USERNAME,
            .credentials.authentication.password = BROKER_PASSWORD,
            .session.keepalive = 0,  // Tắt keepalive để ngăn periodic disconnect
            .network.timeout_ms = 30000,  
            .network.disable_auto_reconnect = false,  // Cho phép reconnect khi thực sự bị disconnect
        };

        // Initialize SSL MQTT client
        mqtt_client = esp_mqtt_client_init(&ssl_mqtt_config);
        esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(mqtt_client);
        
        // SSL connection established - no fallback needed
    } else {
        ESP_LOGE("MQTT", "Failed to connect to WiFi after %d seconds", max_retries);
    }  
    
    // Tắt self-organized để ép làm root
    ESP_ERROR_CHECK(esp_mesh_set_self_organized(false, true));

    // Đăng ký event handler để lắng nghe sự kiện mesh
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));

    // Init mesh
    ESP_ERROR_CHECK(esp_mesh_init());

    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy((uint8_t *) &cfg.mesh_id, MESH_ID, 6);
    cfg.channel = CONFIG_MESH_CHANNEL;

    cfg.router.ssid_len = strlen(CONFIG_MESH_ROUTER_SSID);
    memcpy((uint8_t *) &cfg.router.ssid, CONFIG_MESH_ROUTER_SSID, cfg.router.ssid_len);
    memcpy((uint8_t *) &cfg.router.password, CONFIG_MESH_ROUTER_PASSWD, strlen(CONFIG_MESH_ROUTER_PASSWD));

    cfg.mesh_ap.max_connection = CONFIG_MESH_AP_CONNECTIONS;
    memcpy((uint8_t *) &cfg.mesh_ap.password, CONFIG_MESH_AP_PASSWD, strlen(CONFIG_MESH_AP_PASSWD));
    
    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));
    ESP_ERROR_CHECK(esp_mesh_start());

    ESP_LOGI("ROOT", "Root node configuration completed.\n");
    
    // Tạo task để nhận dữ liệu từ child nodes
    xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL);
}