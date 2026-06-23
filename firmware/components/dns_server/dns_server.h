#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts the wildcard DNS server task.
 * Listens on UDP port 53 and resolves all queries to 192.168.4.1.
 */
void dns_server_start(void);

/**
 * @brief Stops the wildcard DNS server task and cleans up resources.
 */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
