idf_component_register(
    SRCS "main.c" "wifi_ap.c" "http_server.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_netif esp_http_server spiffs dns_server esp_timer
)
