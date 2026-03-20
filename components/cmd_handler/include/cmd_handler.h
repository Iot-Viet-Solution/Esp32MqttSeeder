/**
 * @file cmd_handler.h
 * @brief MQTT command handler – listens for downlink commands from the broker.
 *
 * Subscribes to the following topics (all using the build-time device ID):
 *
 *   cmd/<device_id>/config/heartbeat
 *       Payload: {"interval_ms": <uint>}
 *
 *   cmd/<device_id>/config/counter
 *       Payload: {"interval_ms": <uint>, "counter_id": <int>}
 *
 *   cmd/<device_id>/config/log
 *       Payload: {"interval_ms": <uint>, "level": "<string>"}
 *
 *   cmd/<device_id>/config/device
 *       Payload: {"attribute_name": "<string>", "device_status": "<string>"}
 *
 *   cmd/<device_id>/reboot
 *       Payload: (ignored) – triggers an immediate device reboot.
 *
 *   cmd/<device_id>/ota
 *       Payload: {"url": "<firmware_url>", "md5": "<md5_hex_string>"}
 *       Downloads the firmware binary from <url>, flashes it to the next OTA
 *       partition, and reboots.  The MD5 hash is logged for reference; full
 *       verification support can be added once the OTA scheme is finalised.
 *
 * All config changes are applied to the runtime config (app_runtime_config)
 * and take effect on the next publish cycle of the respective publisher.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Initialise the command handler.
 *
 * Registers the MQTT message callback and subscribes to all command topics.
 * Must be called after mqtt_client_manager_init() and after the broker
 * connection is established.
 *
 * @return ESP_OK on success.
 */
esp_err_t cmd_handler_init(void);
