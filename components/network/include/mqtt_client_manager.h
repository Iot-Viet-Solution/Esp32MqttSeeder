/**
 * @file mqtt_client_manager.h
 * @brief MQTT5 client infrastructure.
 *
 * Owns the esp-mqtt client handle, connects to the configured broker using
 * the device MAC address as the client ID, and provides a publish helper that
 * sets the MQTT5 content-type property to "application/json" automatically.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

/**
 * @brief Initialise the MQTT5 client and begin connecting to the broker.
 *
 * The client ID is derived from the Wi-Fi STA MAC address.
 *
 * @return ESP_OK on success.
 */
esp_err_t mqtt_client_manager_init(void);

/**
 * @brief Return the underlying esp-mqtt client handle.
 *
 * @return Client handle (may be NULL before mqtt_client_manager_init()).
 */
esp_mqtt_client_handle_t mqtt_client_manager_get_client(void);

/**
 * @brief Return whether the MQTT5 client is currently connected to the broker.
 */
bool mqtt_client_manager_is_connected(void);

/**
 * @brief Publish a JSON payload to the given topic using MQTT5.
 *
 * Automatically sets the MQTT5 content-type property to "application/json"
 * before each publish.  The function is safe to call from any task.
 *
 * @param topic   Null-terminated topic string.
 * @param payload Null-terminated JSON payload string.
 * @param qos     MQTT QoS level (0, 1, or 2).
 * @return        Message ID (≥ 0) on success, -1 if not connected or on error.
 */
int mqtt_client_manager_publish(const char *topic, const char *payload, int qos);
