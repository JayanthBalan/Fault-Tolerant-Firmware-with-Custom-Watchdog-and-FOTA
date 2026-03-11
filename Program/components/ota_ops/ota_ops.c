
#include "ota_ops.h"

//Manifest descriptor
typedef struct {
    char version[VERSION_STR_LEN];
    char sha256[SHA256_HEX_LEN + 1];
    uint32_t size;
    char url[FIRMWARE_URL_LEN];
} ota_manifest_t;

//Globals
static EventGroupHandle_t conn_stat;
static int retry_cnt = 0;
static const char *TAG = "OTA_OPS";

//Function Prototypes
static void wifi_event_handler(void*, esp_event_base_t, int32_t, void*);
static esp_err_t version_get(char*, size_t);
static esp_err_t version_set(const char*);
static int version_compare(const char*, const char*);
static esp_err_t fetch_manifest(ota_manifest_t*);
static bool update_decision(const ota_manifest_t*);
void blacklist_version_fetch(const char*);
static bool blacklist_check(const char*);
esp_err_t http_event_handler(esp_http_client_event_t *evt);
void wifi_init_sta(void);
void app_download(void);
void ota_gatekeep(void);

//Initialization point
void ota_ops_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized.");

    wifi_init_sta();
    ota_gatekeep();
    vTaskDelay(pdMS_TO_TICKS(500));
    app_download();
}

//NVS version helpers
static esp_err_t version_get(char *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_OTA_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        strncpy(buf, NVS_VER_DEFAULT, len - 1);
        buf[len - 1] = '\0';
        return ESP_OK;
    }
    err = nvs_get_str(h, NVS_VER_KEY, buf, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        strncpy(buf, NVS_VER_DEFAULT, len - 1);
        buf[len - 1] = '\0';
        return ESP_OK;
    }
    return err;
}

static esp_err_t version_set(const char *ver)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_OTA_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "version_set: nvs_open failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, NVS_VER_KEY, ver);
    if (err == ESP_OK) {
        nvs_commit(h);
    }
    else {
        ESP_LOGE(TAG, "version_set: nvs_set_str failed (%s)", esp_err_to_name(err));
    }
    nvs_close(h);
    return err;
}

static int version_compare(const char *v1, const char *v2)
{
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);
    if (maj1 != maj2) return (maj1 > maj2) ? 1 : -1;
    if (min1 != min2) return (min1 > min2) ? 1 : -1;
    if (pat1 != pat2) return (pat1 > pat2) ? 1 : -1;
    return 0;
}

//Logging Handlers
esp_err_t http_event_handler(esp_http_client_event_t *evt)
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
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER  key=%s  value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA  len=%d", evt->data_len);
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

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_cnt < MAX_RETRY) {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RET_DEL));
            esp_wifi_connect();
            retry_cnt++;
            ESP_LOGI(TAG, "Retry WiFi connection (%d/%d)", retry_cnt, MAX_RETRY);
        } else {
            xEventGroupSetBits(conn_stat, CONN_FAIL);
        }
        ESP_LOGI(TAG, "WiFi disconnected.");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        retry_cnt = 0;
        xEventGroupSetBits(conn_stat, CONN_PASS);
    }
}

//Initialization WiFi
void wifi_init_sta(void)
{
    conn_stat = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid, SSID);
    strcpy((char *)wifi_config.sta.password, PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi init Successful.");

    EventBits_t bits = xEventGroupWaitBits(conn_stat, CONN_PASS | CONN_FAIL, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & CONN_PASS) ESP_LOGI(TAG, "Connected to SSID: %s", SSID);
    else if (bits & CONN_FAIL) ESP_LOGE(TAG, "Failed to connect to SSID: %s", SSID);
    else ESP_LOGE(TAG, "Unexpected WiFi event.");
}

//Manifest v/s JSON
static esp_err_t fetch_manifest(ota_manifest_t *out)
{
    char *resp_buf = (char *)malloc(MANIFEST_BUF_SIZE); //Heap allocation - Dynamic Memory
    if (!resp_buf) {
        ESP_LOGE(TAG, "Manifest: failed to allocate receive buffer.");
        return ESP_ERR_NO_MEM;
    }
    memset(resp_buf, 0, MANIFEST_BUF_SIZE);

    esp_http_client_config_t cfg = {};
    memset(&cfg, 0, sizeof(cfg));
    cfg.url = MANIFEST_URL;
    cfg.event_handler = http_event_handler;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = OTA_HANDSHAKE_TIMEOUT;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Manifest: HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp_buf);
        return ESP_FAIL;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len >= (int)(MANIFEST_BUF_SIZE - 1)) {
        ESP_LOGE(TAG, "Manifest: content-length overflow (%d bytes)", content_len);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        free(resp_buf);
        return ESP_FAIL;
    }

    int read_len = esp_http_client_read(client, resp_buf, MANIFEST_BUF_SIZE - 1);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_len <= 0) {
        ESP_LOGE(TAG, "Manifest: no data received.");
        free(resp_buf);
        return ESP_FAIL;
    }
    resp_buf[read_len] = '\0';
    ESP_LOGI(TAG, "Manifest received:\n%s", resp_buf);

    cJSON *root = cJSON_Parse(resp_buf);
    free(resp_buf);
    resp_buf = NULL;

    if (!root) {
        ESP_LOGE(TAG, "Manifest: JSON parse failed.");
        return ESP_FAIL;
    }

    cJSON *j_ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *j_sha = cJSON_GetObjectItemCaseSensitive(root, "sha256");
    cJSON *j_size = cJSON_GetObjectItemCaseSensitive(root, "size");
    cJSON *j_url = cJSON_GetObjectItemCaseSensitive(root, "url");

    if (!cJSON_IsString(j_ver)  || !j_ver->valuestring  || !cJSON_IsString(j_sha)  || !j_sha->valuestring  || !cJSON_IsNumber(j_size) || !cJSON_IsString(j_url)  || !j_url->valuestring) {
        ESP_LOGE(TAG, "Manifest: Missing/Incorrect Type Fields.");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(ota_manifest_t));
    strncpy(out->version, j_ver->valuestring, sizeof(out->version) - 1);
    strncpy(out->sha256,  j_sha->valuestring, sizeof(out->sha256)  - 1);
    out->size = (uint32_t)j_size->valuedouble;
    strncpy(out->url,     j_url->valuestring, sizeof(out->url)     - 1);

    cJSON_Delete(root);

    ESP_LOGI(TAG, "Manifest — version: %s, size: %lu, sha256: %.16s", out->version, (unsigned long)out->size, out->sha256);
    return ESP_OK;
}

//Update
static bool update_decision(const ota_manifest_t *manifest)
{
    char curr_ver[VERSION_STR_LEN] = NVS_VER_DEFAULT;
    version_get(curr_ver, sizeof(curr_ver));

    ESP_LOGI(TAG, "Version check — server: %s; client: %s", manifest->version, curr_ver);

    if (blacklist_check(manifest->version)) {
        ESP_LOGW(TAG, "Server version %s blacklisted — skipping.", manifest->version);
        return false;
    }

    int cmp = version_compare(manifest->version, curr_ver);
    if (cmp > 0) {
        ESP_LOGI(TAG, "Update: %s -> %s", curr_ver, manifest->version);
        return true;
    } else if (cmp == 0) {
        ESP_LOGI(TAG, "Firmware up to date.");
        return false;
    } else {
        ESP_LOGW(TAG, "Server version < client version. Skip.");
        return false;
    }
}

//Rollback
void app_rollback(void) {
    esp_app_desc_t app_info;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_partition_description(running, &app_info) == ESP_OK) {
        blacklist_version_fetch(app_info.version);
    }
    
    esp_ota_mark_app_invalid_rollback_and_reboot();
}

void app_download(void)
{
    ota_manifest_t manifest = {0};
    if (fetch_manifest(&manifest) != ESP_OK) {
        ESP_LOGE(TAG, "Manifest Fetch Fail. Abort OTA.");
        return;
    }

    if (!update_decision(&manifest)) {
        return;
    }

    char firm_url[sizeof(GITHUB_RAW_BASE) + 1 + FIRMWARE_URL_LEN];
    memset(firm_url, 0, sizeof(firm_url));
    snprintf(firm_url, sizeof(firm_url), GITHUB_RAW_BASE "/%s", manifest.url);
    ESP_LOGI(TAG, "Firmware URL: %s", firm_url);

    esp_http_client_config_t cfg = {};
    memset(&cfg, 0, sizeof(cfg));
    cfg.url = firm_url;
    cfg.event_handler = http_event_handler;
    cfg.keep_alive_enable = true;
    cfg.timeout_ms = OTA_HANDSHAKE_TIMEOUT;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Firmware Connection Fail.");
        esp_http_client_cleanup(client);
        return;
    }
    esp_http_client_fetch_headers(client);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Write partition: %s", update_partition->label);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin fail: %s", esp_err_to_name(ret));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    uint8_t *ota_buf = (uint8_t *)malloc(OTA_BUF_SIZE);
    if (!ota_buf) {
        ESP_LOGE(TAG, "OTA chunk buffer allocation fail.");
        esp_ota_end(ota_handle);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts(&sha_ctx, 0);

    int data_read;
    uint32_t total_bytes = 0;
    bool ota_ok = true;
    
    while ((data_read = esp_http_client_read(client, (char*)ota_buf, OTA_BUF_SIZE)) > 0) {
        ret = esp_ota_write(ota_handle, ota_buf, data_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write fail: %s", esp_err_to_name(ret));
            ota_ok = false;
            break;
        }
        mbedtls_sha256_update(&sha_ctx, ota_buf, data_read);
        total_bytes += (uint32_t)data_read;
    }

    free(ota_buf);
    ota_buf = NULL;

    uint8_t sha_raw[32];
    mbedtls_sha256_finish(&sha_ctx, sha_raw);
    mbedtls_sha256_free(&sha_ctx);

    char sha_hex[SHA256_HEX_LEN + 1] = {0};
    for (int i = 0; i < 32; i++) {
        snprintf(&sha_hex[i * 2], 3, "%02X", sha_raw[i]);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ota_ok && total_bytes != manifest.size) {
        ESP_LOGE(TAG, "Size mismatch — expected: %lu, received: %lu", (unsigned long)manifest.size, (unsigned long)total_bytes);
        ota_ok = false;
    }

    if (ota_ok && strncmp(sha_hex, manifest.sha256, SHA256_HEX_LEN) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch");
        ESP_LOGE(TAG, "  expected: %s", manifest.sha256);
        ESP_LOGE(TAG, "  received: %s", sha_hex);
        ota_ok = false;
    }

    if (ota_ok) {
        ret = esp_ota_end(ota_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "OTA verified (size=%lu bytes, SHA256=OK).", (unsigned long)total_bytes);
            ESP_ERROR_CHECK(esp_ota_set_boot_partition(update_partition));
            ESP_LOGI(TAG, "Boot partition updated. Rebooting.");
            vTaskDelay(pdMS_TO_TICKS(300));
            esp_restart();
        }
        else {
            ESP_LOGE(TAG, "esp_ota_end fail: %s", esp_err_to_name(ret));
        }
    }
    else {
        esp_ota_end(ota_handle);
        ESP_LOGE(TAG, "OTA verification fail. Firmware not applied.");
    }
}

void blacklist_version_fetch(const char *ver)
{
    nvs_handle_t h;
    if (nvs_open(NVS_OTA_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, NVS_BAD_VER_KEY, ver);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "Blacklisted bad firmware version: %s", ver);
    }
}

static bool blacklist_check(const char *ver)
{
    char bad[VERSION_STR_LEN] = {0};
    nvs_handle_t h;
    if (nvs_open(NVS_OTA_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(bad);
    esp_err_t err = nvs_get_str(h, NVS_BAD_VER_KEY, bad, &len);
    nvs_close(h);
    if (err != ESP_OK) return false;
    return (strncmp(ver, bad, VERSION_STR_LEN) == 0);
}

//Rollback Decision
void ota_gatekeep(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;

    esp_app_desc_t app_info;
    bool has_app_desc = (esp_ota_get_partition_description(running, &app_info) == ESP_OK);

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_NEW || ota_state == ESP_OTA_IMG_UNDEFINED) {
            ESP_LOGI(TAG, "New firmware — validating.");
            if (has_app_desc) {
                version_set(app_info.version);
                ESP_LOGI(TAG, "NVS version synced: %s", app_info.version);
            }
            esp_ota_mark_app_valid_cancel_rollback();
        }
        else if (ota_state == ESP_OTA_IMG_VALID) {
            if (has_app_desc) {
                version_set(app_info.version);
            }
            ESP_LOGI(TAG, "Run validated firmware.");
        }
        else if (ota_state == ESP_OTA_IMG_ABORTED) {
            ESP_LOGW(TAG, "Previous OTA aborted — initiate rollback.");
            app_rollback();
        }
        else {
            ESP_LOGW(TAG, "Unknown OTA image state: %d — accept firmware.", ota_state);
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    ESP_LOGI(TAG, "Running partition: %s (type %d, subtype %d, offset 0x%08lx)", running->label, running->type, running->subtype, (unsigned long)running->address);
    if (has_app_desc) {
        ESP_LOGI(TAG, "Firmware version: %s (built %s %s)", app_info.version, app_info.date, app_info.time);
    }

    const esp_partition_t *configured  = esp_ota_get_boot_partition();
    const esp_partition_t *next_update = esp_ota_get_next_update_partition(NULL);
    ESP_LOGI(TAG, "Boot partition: %s", configured->label);
    ESP_LOGI(TAG, "Next update slot: %s", next_update->label);
}
