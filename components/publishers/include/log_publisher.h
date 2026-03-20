/**
 * @file log_publisher.h
 * @brief Periodic log publisher.
 *
 * Topic:   devices/<device_id>/log/<log_level>
 * Payload: {"message":"<log string>","time_stamp":"<ISO-8601-UTC>"}
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Create and start the log publisher FreeRTOS task (pinned to Core 1).
 *
 * @return ESP_OK on success, ESP_FAIL if the task could not be created.
 */
esp_err_t log_publisher_start(void);
