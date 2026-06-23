#pragma once

#include "esp_http_server.h"

/**
 * @brief Starts the HTTP web server for the captive portal, mounts SPIFFS, and registers routes.
 * @return Handle to the HTTP server, or NULL on failure.
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stops the HTTP web server.
 * @param server Handle to the HTTP server to stop.
 */
void stop_webserver(httpd_handle_t server);
