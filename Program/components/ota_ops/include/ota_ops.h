
#ifndef OTA_OPS_H
#define OTA_OPS_H

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
#include "nvs_flash.h"
#include "esp_netif.h"

//Versioning Configuration
#define NVS_VERSION_KEY "version"
#define VER_DEC -1
#define VER_INC 1

//WiFi Configuration
#define SSID "JAYANTH"
#define PASSWORD "Jayrocks"

//OTA Server Configuration
#define SERVER_IP "172.20.181.94"
#define SERVER_PORT "8000"
#define OTA_URL "http://" SERVER_IP ":" SERVER_PORT "/firmware.bin"

//Security Configuration
#define CERT_URL "http://" SERVER_IP ":" SERVER_PORT "/cert.txt"
#define AUTH_BUFFER_SIZE 44
#define VER_INFO_SIZE 6
#define NVS_VERSION_KEY "version"
#define VER_DEC -1
#define VER_INC 1

//Macros
#define CONN_PASS BIT0
#define CONN_FAIL BIT1
#define MAX_RETRY 5

//Timing configuration
#define SYS_POLL_DEL 60000
#define WIFI_RET_DEL 4000
#define OTA_HANDSHAKE_TIMEOUT 30000

//Function Prototypes
void ota_ops_init(void);
void version_update(int8_t);

#endif
