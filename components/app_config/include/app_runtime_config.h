/**
 * @file app_runtime_config.h
 * @brief Runtime-mutable configuration that can be updated via MQTT commands.
 *
 * All values are initialised from the compile-time Kconfig defaults and can be
 * changed at runtime by the cmd_handler component.  Access is protected by a
 * FreeRTOS mutex so this module is safe to call from any task.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Initialise the runtime config with Kconfig defaults.
 *
 * Must be called once before any publisher or cmd_handler accesses the config.
 *
 * @return ESP_OK on success.
 */
esp_err_t app_runtime_config_init(void);

/* ── Heartbeat ────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_heartbeat_interval_ms(void);
void     app_runtime_config_set_heartbeat_interval_ms(uint32_t ms);

/** Copy attribute_name string into @p buf (size @p size). */
void app_runtime_config_get_attribute_name(char *buf, size_t size);
void app_runtime_config_set_attribute_name(const char *name);

/** Copy device_status string into @p buf (size @p size). */
void app_runtime_config_get_device_status(char *buf, size_t size);
void app_runtime_config_set_device_status(const char *status);

/* ── Counter ──────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_counter_interval_ms(void);
void     app_runtime_config_set_counter_interval_ms(uint32_t ms);

int  app_runtime_config_get_counter_id(void);
void app_runtime_config_set_counter_id(int id);

/** UTC hour (0-23) at which shoot_count is reset; 255 = disabled. */
uint8_t app_runtime_config_get_counter_reset_hour(void);
void    app_runtime_config_set_counter_reset_hour(uint8_t hour);

/* ── Log ──────────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_log_interval_ms(void);
void     app_runtime_config_set_log_interval_ms(uint32_t ms);

/** Copy log_level string into @p buf (size @p size). */
void app_runtime_config_get_log_level(char *buf, size_t size);
void app_runtime_config_set_log_level(const char *level);
