/**
 * @file mqtt_client_manager.h
 * @brief MQTT5 client infrastructure.
 *
 * Owns the esp-mqtt client handle, connects to the configured broker using
 * the device MAC address as the client ID, and provides a publish helper that
 * sets the MQTT5 content-type property to "application/json" automatically.
 *
 * Subscriptions registered via mqtt_client_manager_subscribe() are
 * automatically re-established after every broker reconnect.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "mqtt_client.h"

/**
 * @brief Callback invoked for every incoming MQTT message.
 *
 * @param topic     Topic string (NOT null-terminated; use topic_len).
 * @param topic_len Length of topic in bytes.
 * @param data      Payload bytes (NOT null-terminated; use data_len).
 * @param data_len  Length of payload in bytes.
 */
typedef void (*mqtt_message_handler_t)(const char *topic, int topic_len,
                                       const char *data,  int data_len);

/**
 * @brief Callback invoked every time the MQTT client successfully connects
 *        to the broker (including reconnects after a drop).
 */
typedef void (*mqtt_connected_handler_t)(void);

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

/**
 * @brief Subscribe to an MQTT topic.
 *
 * The subscription is stored and automatically re-established after every
 * broker reconnect.  Safe to call after mqtt_client_manager_init().
 *
 * @param topic Null-terminated topic string (may include + and # wildcards).
 * @param qos   MQTT QoS level (0, 1, or 2).
 * @return      Message ID (≥ 0) on success, -1 on error.
 */
int mqtt_client_manager_subscribe(const char *topic, int qos);

/**
 * @brief Register a callback to receive all incoming MQTT messages.
 *
 * Only one handler can be active at a time; a subsequent call replaces the
 * previous registration.  Pass NULL to deregister.
 *
 * @param handler Callback function, or NULL.
 */
void mqtt_client_manager_set_message_handler(mqtt_message_handler_t handler);

/**
 * @brief Register a callback that is invoked on every successful broker
 *        connection (initial connect and every reconnect).
 *
 * Only one handler can be active at a time; a subsequent call replaces the
 * previous registration.  Pass NULL to deregister.
 *
 * @param handler Callback function, or NULL.
 */
void mqtt_client_manager_set_connected_handler(mqtt_connected_handler_t handler);

/**
 * @brief Return the current MQTT outbox queue size in bytes.
 *
 * The outbox holds QoS > 0 messages waiting for broker acknowledgement.
 * A persistently growing outbox indicates the broker is not acknowledging
 * messages fast enough, or that the client has been disconnected.
 *
 * @return Outbox size in bytes (≥ 0), or -1 if the client is not initialised.
 */
int mqtt_client_manager_get_outbox_size(void);

