#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dns_server.h"

static const char *TAG = "dns_server";

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

static int dns_socket = -1;
static TaskHandle_t dns_task_handle = NULL;
static volatile bool dns_running = false;

static int parse_dns_name(const uint8_t *buffer, int offset, int max_len, char *name_out, int name_max) {
    int name_len = 0;
    int curr = offset;
    
    while (curr < max_len) {
        uint8_t len = buffer[curr];
        if (len == 0) {
            curr++; // Skip terminating zero
            break;
        }
        
        // Handle compression pointer (though rare in queries)
        if ((len & 0xC0) == 0xC0) {
            return -1; // We don't support compression pointers in query names
        }
        
        if (curr + 1 + len > max_len) {
            return -1; // Overflow
        }
        
        if (name_len > 0 && name_len < name_max - 1) {
            name_out[name_len++] = '.';
        }
        
        for (int i = 0; i < len && name_len < name_max - 1; i++) {
            name_out[name_len++] = (char)buffer[curr + 1 + i];
        }
        
        curr += 1 + len;
    }
    
    if (name_len < name_max) {
        name_out[name_len] = '\0';
    } else {
        name_out[name_max - 1] = '\0';
    }
    
    return curr; // Returns offset after the name
}

static const char* allowed_dns_domains[] = {
    "carb.by",
    "www.carb.by",
    "connectivitycheck.gstatic.com",
    "connectivitycheck.android.com",
    "clients3.google.com",
    "captive.apple.com",
    "www.msftconnecttest.com",
    "www.msftncsi.com"
};

static void dns_server_task(void *pvParameters) {
    uint8_t rx_buffer[512];
    struct sockaddr_storage source_addr;
    socklen_t addr_len = sizeof(source_addr);
    
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    int opt = 1;
    setsockopt(dns_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    int err = bind(dns_socket, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(dns_socket);
        dns_socket = -1;
        dns_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "DNS Server socket bound to port 53, listening...");
    dns_running = true;
    
    while (dns_running) {
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&source_addr, &addr_len);
        if (len < 0) {
            if (dns_running) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            }
            break;
        }
        
        if (len < sizeof(dns_header_t)) {
            continue;
        }
        
        dns_header_t *header = (dns_header_t *)rx_buffer;
        uint16_t qd_count = ntohs(header->qd_count);
        
        if (qd_count != 1) {
            continue; // Only support single question queries
        }
        
        char domain_name[128];
        int offset = parse_dns_name(rx_buffer, sizeof(dns_header_t), len, domain_name, sizeof(domain_name));
        if (offset < 0 || offset + 4 > len) {
            ESP_LOGW(TAG, "Failed to parse query domain name");
            continue;
        }
        
        bool should_resolve = false;
        for (size_t i = 0; i < sizeof(allowed_dns_domains) / sizeof(allowed_dns_domains[0]); i++) {
            if (strcasecmp(domain_name, allowed_dns_domains[i]) == 0) {
                should_resolve = true;
                break;
            }
        }
        
        if (!should_resolve) {
            continue;
        }
        
        ESP_LOGI(TAG, "DNS Query resolved: %s -> 192.168.4.1", domain_name);
        
        int q_len = offset + 4; // Original question length (name + QTYPE + QCLASS)
        uint8_t tx_buffer[512];
        memcpy(tx_buffer, rx_buffer, q_len); // Copy header and question verbatim
        
        dns_header_t *resp_header = (dns_header_t *)tx_buffer;
        resp_header->flags = htons(0x8580); // Response, Authoritative, Recursion Desired & Available
        resp_header->an_count = htons(1);
        resp_header->ns_count = 0;
        resp_header->ar_count = 0;
        
        uint8_t answer[] = {
            0xC0, 0x0C,            // Name pointer to domain name in question (offset 12)
            0x00, 0x01,            // Type A (IPv4 address)
            0x00, 0x01,            // Class IN (Internet)
            0x00, 0x00, 0x00, 0x3C, // TTL 60 seconds
            0x00, 0x04,            // RDLENGTH 4 bytes
            192, 168, 4, 1         // RDATA (IPv4 address 192.168.4.1)
        };
        
        if (q_len + sizeof(answer) <= sizeof(tx_buffer)) {
            memcpy(tx_buffer + q_len, answer, sizeof(answer));
            int resp_len = q_len + sizeof(answer);
            
            int sent = sendto(dns_socket, tx_buffer, resp_len, 0, (struct sockaddr *)&source_addr, addr_len);
            if (sent < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            }
        } else {
            ESP_LOGE(TAG, "Response packet size exceeded buffer");
        }
    }
    
    if (dns_socket >= 0) {
        close(dns_socket);
        dns_socket = -1;
    }
    ESP_LOGI(TAG, "DNS Server task exiting.");
    dns_task_handle = NULL;
    vTaskDelete(NULL);
}

void dns_server_start(void) {
    if (dns_task_handle != NULL) {
        ESP_LOGW(TAG, "DNS server is already running");
        return;
    }
    xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
}

void dns_server_stop(void) {
    if (dns_task_handle == NULL) {
        ESP_LOGW(TAG, "DNS server is not running");
        return;
    }
    dns_running = false;
    if (dns_socket >= 0) {
        shutdown(dns_socket, SHUT_RDWR);
        close(dns_socket);
        dns_socket = -1;
    }
}
