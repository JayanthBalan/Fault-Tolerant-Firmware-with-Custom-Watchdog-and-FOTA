
#include "ota_ops.h"

static EventGroupHandle_t conn_stat;
static int retry_cnt = 0;
static const char *TAG = "OTA_OPS";
const uint8_t auth_token[] = "SECURE_KEY---JUGGERNAUT_AUTH_2026_X99";

void wifi_init_sta(void);
void app_download(void);
void ota_gatekeep(void);
bool token_handshake(void);
int8_t version_data(void);
esp_err_t http_event_handler(esp_http_client_event_t*);
static void wifi_event_handler(void*, esp_event_base_t, int32_t, void*);
int8_t client_authenticate(uint8_t*, const uint8_t*);

void ota_ops_init(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS Initialized.");

    //Initialize WiFi
    wifi_init_sta();

    //Call OTA Update
    ota_gatekeep();
    
    vTaskDelay(pdMS_TO_TICKS(5000));

    //OTA Function
    app_download();
}

void version_update(int8_t type)
{
    nvs_handle_t nvs_handle_0;
    ESP_ERROR_CHECK(nvs_open("ota", NVS_READWRITE, &nvs_handle_0));

    if(type == 0) {
        ESP_ERROR_CHECK(nvs_set_u8(nvs_handle_0, NVS_VERSION_KEY, 1));
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_0));
        nvs_close(nvs_handle_0);
        return;
    }

    uint8_t ver = 1;
    esp_err_t err = nvs_get_u8(nvs_handle_0, NVS_VERSION_KEY, &ver);
    switch (err) {
        case ESP_OK:
            ESP_LOGI("TAG", "Client Version: %d", ver);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGW("TAG", "Initialized");
            ESP_ERROR_CHECK(nvs_set_u8(nvs_handle_0, NVS_VERSION_KEY, 1));
            nvs_close(nvs_handle_0);
            version_update(0);
            return;
            break;
        case ESP_ERR_NVS_INVALID_NAME:
            ESP_LOGE("TAG", "Invalid name!");
            nvs_close(nvs_handle_0);
            return;
            break;
        default:
            ESP_LOGE("TAG", "Error (%s) reading!", esp_err_to_name(err));
            nvs_close(nvs_handle_0);
            return;
    }

    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle_0, NVS_VERSION_KEY, (int8_t)ver + type));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle_0));

    nvs_close(nvs_handle_0);
}

int8_t version_data(void) {
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("ota", NVS_READWRITE, &nvs_handle));

    uint8_t ver = 0;
    esp_err_t err = nvs_get_u8(nvs_handle, NVS_VERSION_KEY, &ver);
    switch (err) {
        case ESP_OK:
            ESP_LOGI(TAG, "Client Firmware Version: %d", ver);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            ESP_LOGW(TAG, "Uninitialized");
            nvs_close(nvs_handle);
            version_update(0);
            return 1;
            break;
        case ESP_ERR_NVS_INVALID_NAME:
            ESP_LOGE(TAG, "Invalid name!");
            nvs_close(nvs_handle);
            return -1;
            break;
        default:
            ESP_LOGE(TAG, "Error (%s) reading!", esp_err_to_name(err));
            nvs_close(nvs_handle);
            return -2;
    }

    nvs_close(nvs_handle);
    return (int8_t)ver;
}

esp_err_t http_event_handler(esp_http_client_event_t *evt) //HTTP Event Callback
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) //WiFi Event Callback
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_cnt < MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RET_DEL));
            esp_wifi_connect();
            retry_cnt++;
            ESP_LOGI(TAG, "Retry connecting to WiFi...");
        } else {
            xEventGroupSetBits(conn_stat, CONN_FAIL);
        }
        ESP_LOGI(TAG,"Connection to WiFi failed");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        retry_cnt = 0;
        xEventGroupSetBits(conn_stat, CONN_PASS);
    }
}

void wifi_init_sta(void) //WiFi Initialization
{
    conn_stat = xEventGroupCreate(); //Signalling

    //TCP/IP Stack Initialization
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    //WiFi Driver Initialization
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    //Register Event Handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    //WiFi Credentials and Security
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, SSID);
    strcpy((char*)wifi_config.sta.password, PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    //Begin WiFi State Machine
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi init finished.");

    //Block and Hard Sync
    EventBits_t bits = xEventGroupWaitBits(conn_stat, CONN_PASS | CONN_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);

    //Status Info
    if (bits & CONN_PASS) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", SSID);
    } else if (bits & CONN_FAIL) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", SSID);
    } else {
        ESP_LOGE(TAG, "Unexpected WiFi event");
    }
}

int8_t client_authenticate(uint8_t *target, const uint8_t *reference) //Token Comparison
{
    uint8_t ver_info = (target[1] -'0')*10 + (target[2] - '0');
    ESP_LOGI(TAG, "Server Firmware Version: %d", ver_info);

    size_t token_len = strlen((char*)reference);
    if (memcmp(&target[6], reference, AUTH_BUFFER_SIZE - VER_INFO_SIZE) != 0) {
        ESP_LOGI(TAG, "Token Mismatch");
        ESP_LOGI(TAG, "Expected: %s", reference);
        ESP_LOGI(TAG, "Received: %.*s", (int)token_len, &target[6]);
        return -1; //Failure
    }

    int8_t curr_ver = version_data();
    if(curr_ver == 0) {
        ESP_LOGW(TAG, "Client Firmware Version: Uninitialized");
        return 0; //Success
    }
    else if(curr_ver < 0) {
        ESP_LOGE(TAG, "Auth Failed: Client Version Error.");
        return -2; //Failure
    }
    else if(ver_info < curr_ver) {
        ESP_LOGI(TAG, "Version mismatch: server=%d, client=%d", ver_info, curr_ver);
        return 1; //Version Mismatch
    }

    ESP_LOGI(TAG, "Auth Success: Token Verified.");
    return 0;
}

bool token_handshake(void) {
    //Configure HTTP/TLS Client for Token Handshake
    esp_http_client_config_t config_cert = {};
    memset(&config_cert, 0, sizeof(config_cert));
    config_cert.url = CERT_URL;
    config_cert.event_handler = http_event_handler;
    config_cert.keep_alive_enable = true;
    config_cert.timeout_ms = OTA_HANDSHAKE_TIMEOUT;
    uint8_t buffer[AUTH_BUFFER_SIZE] = {0};

    //Begin Token Handshake
    esp_http_client_handle_t client_cert = esp_http_client_init(&config_cert);
    esp_err_t err = esp_http_client_open(client_cert, 0);

    //Handshake Process
    if(err == ESP_OK) {
        esp_http_client_fetch_headers(client_cert);
        int len = esp_http_client_read(client_cert, (char*)buffer, AUTH_BUFFER_SIZE);
        esp_http_client_close(client_cert);
        esp_http_client_cleanup(client_cert);

        int8_t auth_status = client_authenticate(buffer, auth_token);
        if (len > 0 && auth_status == 0) {
            return true;
        }
        else if (len <= 0) {
            ESP_LOGE(TAG, "Auth Failed: No Data Received.");
            return false;
        }
        else if (auth_status == 1) {
            return false;
        }
        else if (auth_status == -1) {
            return false;
        }
        else if (auth_status == -2) {
            return false;
        }
    }
    else {
        ESP_LOGE(TAG, "Auth Failed: HTTP Error.");
        esp_http_client_close(client_cert);
        esp_http_client_cleanup(client_cert);
        return false;
    }

    return false;
}

void app_download(void) //OTA App Download
{

    if(token_handshake() == true) {

        //Configure HTTP/TLS Client for OTA engine
        esp_http_client_config_t config_firm = {};
        memset(&config_firm, 0, sizeof(config_firm));
        config_firm.url = OTA_URL;
        config_firm.event_handler = http_event_handler;
        config_firm.keep_alive_enable = true;
        config_firm.timeout_ms = OTA_HANDSHAKE_TIMEOUT;
        esp_http_client_handle_t client_firm = esp_http_client_init(&config_firm);
        if (esp_http_client_open(client_firm, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            esp_http_client_cleanup(client_firm);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RET_DEL));
            return;
        }
        esp_http_client_fetch_headers(client_firm);

        //Fetch Partition for Update
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        ESP_LOGI(TAG, "Update Partition: %s", update_partition->label);

        //Begin OTA Download
        esp_ota_handle_t ota_handle = 0;
        esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
            esp_http_client_close(client_firm);
            esp_http_client_cleanup(client_firm);
            vTaskDelay(pdMS_TO_TICKS(SYS_POLL_DEL));
            return;
        }
    
        //OTA Process
        uint8_t ota_buf[1024];
        int data_read;
        bool ota_ok = true;
        while ((data_read = esp_http_client_read(client_firm, (char *)ota_buf, sizeof(ota_buf))) > 0) {
            ret = esp_ota_write(ota_handle, ota_buf, data_read);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
                ota_ok = false;
                return;
            }
        }
        
        //End OTA Process
        if (ota_ok) {
            ret = esp_ota_end(ota_handle);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "HTTP OTA finished; Set boot partition.");
                ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
                ESP_LOGI(TAG, "OTA Success, Rebooting.");
                vTaskDelay(pdMS_TO_TICKS(3000));
                esp_restart();
            }
            else {
                ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
            }
        }
        else {
            esp_ota_end(ota_handle);
        }

        esp_http_client_close(client_firm);
        esp_http_client_cleanup(client_firm);
    }
}

void ota_gatekeep(void) //OTA Implementation
{
    //Active Partition
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    //Rollback Protection
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_NEW || ota_state == ESP_OTA_IMG_UNDEFINED) {
            ESP_LOGI(TAG, "New Firmware.");
            version_update(VER_INC);
            esp_ota_mark_app_valid_cancel_rollback();
        }
        else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "Running validated firmware (state: VALID)");
        }
        else if (ota_state == ESP_OTA_IMG_ABORTED) {
            ESP_LOGW(TAG, "Previous OTA was aborted (state: ABORTED)");
            int8_t curr_ver = version_data();
            if (curr_ver > 0) {
                ESP_LOGE(TAG, "Rolling back to previous version");
                version_update(VER_DEC);
                esp_ota_mark_app_invalid_rollback_and_reboot();
            } else {
                ESP_LOGW(TAG, "No valid previous version, staying on current firmware");
                esp_ota_mark_app_valid_cancel_rollback();
            }
        }
        else {
            ESP_LOGW(TAG, "Unknown OTA image state: %d", ota_state);
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
    
    ESP_LOGI(TAG, "Running partition: %s (type %d, subtype %d, offset 0x%08lx)", running->label, running->type, running->subtype, running->address);

    //Configured Partitions
    const esp_partition_t *configured = esp_ota_get_boot_partition();
    const esp_partition_t *next_update = esp_ota_get_next_update_partition(NULL);

    ESP_LOGI(TAG, "Configured boot partition: %s", configured->label);
    ESP_LOGI(TAG, "Next update partition: %s", next_update->label);

    //App Description
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
        ESP_LOGI(TAG, "Compile time: %s %s", running_app_info.date, running_app_info.time);
    }
}
