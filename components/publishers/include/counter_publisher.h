/**
 * @file counter_publisher.h
 * @brief Periodic counter publisher.
 *
 * Topic:   uplink/v3/di/<counter_id>
 * Payload: {"time_stamp":"<ISO-8601-UTC>","shoot_count":<N>,"pulse_time":<ms>}
 *
 * Both <counter_id> and the publish interval are configurable via menuconfig.
 */
#pragma once

#include "esp_err.h"

/**
 * @brief Create and start the counter FreeRTOS task (pinned to Core 1).
 *
 * @return ESP_OK on success, ESP_FAIL if the task could not be created.
 */
esp_err_t counter_publisher_start(void);
