#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the WiFi Access Point.
 */
void wifi_init_softap(void);

/**
 * @brief Get the count of currently connected stations.
 * @return Number of connected devices.
 */
int wifi_ap_get_client_count(void);

#ifdef __cplusplus
}
#endif
