/**
 * @file app_config.h
 * @brief Centralised compile-time configuration derived from Kconfig (menuconfig).
 *
 * All tuneable parameters are surfaced through sdkconfig via Kconfig.projbuild.
 * This header maps them to concise macro names used throughout the firmware.
 */
#pragma once

/* ── WiFi ─────────────────────────────────────────────────────────────────── */
#define APP_WIFI_SSID           CONFIG_SEEDER_WIFI_SSID
#define APP_WIFI_PASSWORD       CONFIG_SEEDER_WIFI_PASSWORD
#define APP_WIFI_MAX_RETRY      CONFIG_SEEDER_WIFI_MAX_RETRY

/* ── MQTT Broker ──────────────────────────────────────────────────────────── */
#define APP_MQTT_BROKER_URI     CONFIG_SEEDER_MQTT_BROKER_URI
#define APP_MQTT_USERNAME       CONFIG_SEEDER_MQTT_USERNAME
#define APP_MQTT_PASSWORD       CONFIG_SEEDER_MQTT_PASSWORD
#define APP_MQTT_KEEPALIVE_SEC  CONFIG_SEEDER_MQTT_KEEPALIVE_SEC
/** QoS level used for all publishes. */
#define APP_MQTT_QOS            1

/* ── Device Identity ──────────────────────────────────────────────────────── */
/** String wildcard for heartbeat (uplink/heartbeat/v1/<id>) and
 *  first wildcard for log (devices/<id>/log/<level>). */
#define APP_DEVICE_ID           CONFIG_SEEDER_DEVICE_ID
/** Device type string sent in the devices/init payload on broker connect. */
#define APP_DEVICE_TYPE         CONFIG_SEEDER_DEVICE_TYPE
#define APP_ATTRIBUTE_NAME      CONFIG_SEEDER_ATTRIBUTE_NAME
#define APP_DEVICE_STATUS       CONFIG_SEEDER_DEVICE_STATUS

/* ── Heartbeat Publisher ──────────────────────────────────────────────────── */
#define APP_HEARTBEAT_INTERVAL_MS   CONFIG_SEEDER_HEARTBEAT_INTERVAL_MS

/* ── Counter Publisher ────────────────────────────────────────────────────── */
/** Numeric wildcard for uplink/v3/di/<counter_id>. */
#define APP_COUNTER_ID              CONFIG_SEEDER_COUNTER_ID
#define APP_COUNTER_INTERVAL_MS     CONFIG_SEEDER_COUNTER_INTERVAL_MS
/** UTC hour (0-23) to reset shoot_count; 255 = disabled. */
#define APP_COUNTER_RESET_HOUR      CONFIG_SEEDER_COUNTER_RESET_HOUR

/* ── Log Publisher ────────────────────────────────────────────────────────── */
/** Log-level string for devices/<id>/log/<level>. */
#define APP_LOG_LEVEL           CONFIG_SEEDER_LOG_LEVEL
#define APP_LOG_INTERVAL_MS     CONFIG_SEEDER_LOG_INTERVAL_MS
