#include <stdio.h>
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "wifi_ap.h"
#include "dns_server.h"
#include "http_server.h"

static const char *TAG = "main";

void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32-C3 Captive Portal application...");

    // 1. Initialize Non-Volatile Storage (NVS)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncation or upgrade detected. Erasing and retrying...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Create the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3. Start WiFi in SoftAP mode
    wifi_init_softap();

    // 4. Start DNS Server (redirecting UDP/53 queries to 192.168.4.1)
    dns_server_start();

    // 5. Start HTTP Server to handle captive portal redirects & API endpoints
    httpd_handle_t server = start_webserver();
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP Server!");
    }

    // 6. Log configuration instructions
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "CAPTIVE PORTAL ONLINE");
    ESP_LOGI(TAG, "Connect to SSID: %s", CONFIG_PORTAL_SSID);
    ESP_LOGI(TAG, "Navigate to URL: http://192.168.4.1");
    ESP_LOGI(TAG, "=================================================");
}
