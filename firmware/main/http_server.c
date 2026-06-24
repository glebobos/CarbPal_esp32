#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "wifi_ap.h"
#include "http_server.h"

static const char *TAG = "http_server";

static const char* get_content_type(const char *path) {
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".ico")) return "image/x-icon";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".jpg") || strstr(path, ".jpeg")) return "image/jpeg";
    if (strstr(path, ".svg")) return "image/svg+xml";
    return "text/plain";
}

static esp_err_t serve_file(httpd_req_t *req, const char *filepath) {
    char gz_filepath[128];
    snprintf(gz_filepath, sizeof(gz_filepath), "%s.gz", filepath);
    
    struct stat st;
    bool is_gz = false;
    const char *final_path = filepath;
    
    if (stat(gz_filepath, &st) == 0) {
        is_gz = true;
        final_path = gz_filepath;
    } else if (stat(filepath, &st) != 0) {
        ESP_LOGE(TAG, "File not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "{\"error\":\"not found\"}");
        return ESP_FAIL;
    }
    
    FILE *fd = fopen(final_path, "r");
    if (!fd) {
        ESP_LOGE(TAG, "Failed to open file: %s", final_path);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read file");
        return ESP_FAIL;
    }
    
    // Set headers
    httpd_resp_set_type(req, get_content_type(filepath));
    if (is_gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    
    // Add cache controls
    if (strstr(filepath, "/assets/")) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    } else if (strstr(filepath, "index.html")) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
    }
    
    char chunk[1024];
    size_t read_bytes;
    do {
        read_bytes = fread(chunk, 1, sizeof(chunk), fd);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(fd);
                ESP_LOGE(TAG, "File sending failed");
                httpd_resp_sendstr_chunk(req, NULL);
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    
    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* HTTP GET Handlers */

static bool is_custom_host(httpd_req_t *req) {
    char host[64];
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) == ESP_OK) {
        if (strstr(host, "carb.by") != NULL) {
            return true;
        }
    }
    return false;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET %s - redirecting to portal home (carb.by)", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://carb.by/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t root_get_handler(httpd_req_t *req) {
    if (!is_custom_host(req)) {
        return captive_redirect_handler(req);
    }
    ESP_LOGI(TAG, "GET / - serving portal index.html");
    esp_err_t ret = serve_file(req, "/spiffs/index.html");
    return ret;
}

// Redundant/unused handlers removed

static esp_err_t api_info_get_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "GET /api/info - serving telemetry");
    
    char json_response[256];
    snprintf(json_response, sizeof(json_response),
             "{\"ssid\":\"%s\",\"ip\":\"192.168.4.1\",\"clients\":%d,\"heap_free\":%lu,\"uptime_s\":%lld,\"idf_version\":\"%s\",\"chip\":\"%s\"}",
             CONFIG_PORTAL_SSID,
             wifi_ap_get_client_count(),
             (unsigned long)esp_get_free_heap_size(),
             (long long)(esp_timer_get_time() / 1000000),
             esp_get_idf_version(),
             "ESP32-C3");
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}

static float s_diff_values[3] = {0.0f, 0.0f, 0.0f};

static esp_err_t api_data_get_handler(httpd_req_t *req) {
    for (int i = 0; i < 3; i++) {
        // Generate random fluctuation between -0.4 and +0.4
        float dev = (((float)(esp_random() % 1000) / 1000.0f) - 0.5f) * 0.8f;
        s_diff_values[i] += dev;
        // Keep them bounded in typical ranges (-30 to +30 kPa)
        if (s_diff_values[i] < -30.0f) s_diff_values[i] = -30.0f;
        if (s_diff_values[i] > 30.0f) s_diff_values[i] = 30.0f;
    }
    
    char json_response[128];
    snprintf(json_response, sizeof(json_response),
             "{\"v1\":%.2f,\"v2\":%.2f,\"v3\":%.2f,\"v4\":0.00}",
             s_diff_values[0], s_diff_values[1], s_diff_values[2]);
             
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, json_response);
    return ESP_OK;
}


static esp_err_t catch_all_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "Catch-all handler matched: %s", req->uri);
    
    // Serve Vite static assets
    if (strncmp(req->uri, "/assets/", 8) == 0) {
        char filepath[128];
        const char *quest = strchr(req->uri, '?');
        size_t path_len = quest ? (quest - req->uri) : strlen(req->uri);
        if (path_len >= sizeof(filepath) - 8) {
            path_len = sizeof(filepath) - 9;
        }
        snprintf(filepath, sizeof(filepath), "/spiffs%.*s", (int)path_len, req->uri);
        return serve_file(req, filepath);
    }
    
    // Default fallback - redirect to the portal home page
    return captive_redirect_handler(req);
}

/* SPIFFS Mounting */

static esp_err_t mount_spiffs(void) {
    ESP_LOGI(TAG, "Mounting SPIFFS partition...");
    
    esp_vfs_spiffs_conf_t conf = {
      .base_path = "/spiffs",
      .partition_label = "storage",
      .max_files = 5,
      .format_if_mount_failed = false
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format SPIFFS filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to locate SPIFFS partition ('storage')");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS Partition mounted. Size: total=%d B, used=%d B, free=%d B", 
                 total, used, total - used);
    } else {
        ESP_LOGE(TAG, "Failed to query SPIFFS statistics (%s)", esp_err_to_name(ret));
    }
    
    return ESP_OK;
}

/* HTTP URI Registrations */

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t api_info_uri = {
    .uri       = "/api/info",
    .method    = HTTP_GET,
    .handler   = api_info_get_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t api_data_uri = {
    .uri       = "/api/data",
    .method    = HTTP_GET,
    .handler   = api_data_get_handler,
    .user_ctx  = NULL
};

// Redundant/unused URI configurations removed

static const httpd_uri_t catch_all_uri = {
    .uri       = "/*",
    .method    = HTTP_GET,
    .handler   = catch_all_handler,
    .user_ctx  = NULL
};

/* Start and Stop Server APIs */

httpd_handle_t start_webserver(void) {
    if (mount_spiffs() != ESP_OK) {
        ESP_LOGW(TAG, "Starting web server without functional SPIFFS storage");
    }
    
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 16;
    config.max_open_sockets  = 7;
    config.lru_purge_enable  = true;
    config.stack_size        = 8192;
    config.uri_match_fn      = httpd_uri_match_wildcard;
    
    ESP_LOGI(TAG, "Starting HTTP Daemon on port %d...", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Registering HTTP routing table...");
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &api_info_uri);
        httpd_register_uri_handler(server, &api_data_uri);
        httpd_register_uri_handler(server, &catch_all_uri);
        return server;
    }
    
    ESP_LOGE(TAG, "Failed to start HTTP Daemon!");
    return NULL;
}

void stop_webserver(httpd_handle_t server) {
    if (server) {
        httpd_stop(server);
        ESP_LOGI(TAG, "HTTP Server stopped.");
    }
    esp_vfs_spiffs_unregister("storage");
    ESP_LOGI(TAG, "SPIFFS storage unmounted.");
}
