/**
 * @file wifi_manager.h
 * @brief WiFi infrastructure – station-mode connection with automatic retry.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialise and connect WiFi in STA mode.
 *
 * Blocks until an IP address is obtained or the maximum number of retries
 * (APP_WIFI_MAX_RETRY) is exhausted.
 *
 * @return ESP_OK on successful connection, ESP_FAIL otherwise.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Return the RSSI of the current AP connection in dBm.
 *
 * @return RSSI value, or 0 if not connected / unavailable.
 */
int8_t wifi_manager_get_rssi(void);
