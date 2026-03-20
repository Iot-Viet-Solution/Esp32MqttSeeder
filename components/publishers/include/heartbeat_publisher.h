/**
 * @file heartbeat_publisher.h
 * @brief Periodic heartbeat publisher.
 *
 * Topic:   uplink/heartbeat/v1/<device_id>
 * Payload: {"serial_no":"...","attribute_name":"...","device_status":"...",
 *            "time_stamp":"<ISO-8601-UTC>","rssi":<dBm>}
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Create and start the heartbeat FreeRTOS task (pinned to Core 1).
 *
 * @return ESP_OK on success, ESP_FAIL if the task could not be created.
 */
esp_err_t heartbeat_publisher_start(void);
