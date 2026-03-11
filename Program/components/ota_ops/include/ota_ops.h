#ifndef OTA_OPS_H
#define OTA_OPS_H

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"

//NVS
#define NVS_OTA_NAMESPACE "ota"
#define NVS_VER_KEY "fw_version"
#define NVS_VER_DEFAULT "0.0.0"

//WiFi
#define SSID "Jayanth"
#define PASSWORD "Jayrocks"

//GitHub
#define GITHUB_USER "JayanthBalan"
#define GITHUB_REPO "Fault-Tolerant-Firmware-with-Custom-Watchdog-and-FOTA"
#define GITHUB_BRANCH "enhancement"
#define GITHUB_RAW_BASE "https://raw.githubusercontent.com/" GITHUB_USER "/" GITHUB_REPO "/" GITHUB_BRANCH
#define MANIFEST_URL GITHUB_RAW_BASE "/manifest.json"
#define NVS_BAD_VER_KEY "bad_version"

//Field Sizes
#define MANIFEST_BUF_SIZE 512
#define VERSION_STR_LEN 16
#define SHA256_HEX_LEN 64
#define FIRMWARE_URL_LEN 128
#define OTA_BUF_SIZE 4096

//WiFi Event bits
#define CONN_PASS BIT0
#define CONN_FAIL BIT1
#define MAX_RETRY 5

//Timing
#define WIFI_RET_DEL 400
#define OTA_HANDSHAKE_TIMEOUT 30000

//Public API
void ota_ops_init(void);
void app_rollback(void);

#endif