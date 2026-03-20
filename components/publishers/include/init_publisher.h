/**
 * @file init_publisher.h
 * @brief Device initialisation publisher.
 *
 * Publishes a single message to the @c devices/init topic every time the
 * device successfully connects (or reconnects) to the MQTT broker.
 *
 * Topic:   devices/init
 * Payload: {"time_stamp": "<ISO-8601-UTC>", "device_type": "<configured-type>"}
 *
 * The device type is set via the @c SEEDER_DEVICE_TYPE Kconfig option.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Register the init publisher.
 *
 * Hooks into the MQTT connected event so that @c devices/init is published
 * automatically on every broker connect.  Must be called after
 * mqtt_client_manager_init().
 *
 * @return ESP_OK on success.
 */
esp_err_t init_publisher_start(void);
