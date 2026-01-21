
idf_component_register(
    SRCS "ota_ops.c"
    INCLUDE_DIRS "inc"
    REQUIRES esp_wifi nvs_flash esp_netif esp_event esp_http_client app_update ota_ops
)
