#include "heartbeat_publisher.h"
#include "app_config.h"
#include "app_runtime_config.h"
#include "mqtt_client_manager.h"
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "heartbeat_pub";

/* Stack-allocated, fixed-size JSON buffer – safe, no heap fragmentation. */
#define HEARTBEAT_JSON_BUF_SIZE  256
#define HEARTBEAT_TOPIC_BUF_SIZE 128

/* ── Publisher task ───────────────────────────────────────────────────────── */
static void heartbeat_task(void *pvParameters)
{
    char topic[HEARTBEAT_TOPIC_BUF_SIZE];
    char json_buf[HEARTBEAT_JSON_BUF_SIZE];
    char timestamp[25]; /* "YYYY-MM-DDTHH:MM:SSZ" + NUL */
    char attribute_name[64];
    char device_status[32];

    /* Topic uses the compile-time device ID (does not change at runtime). */
    snprintf(topic, sizeof(topic), "uplink/heartbeat/v1/%s", APP_DEVICE_ID);

    ESP_LOGI(TAG, "Heartbeat task started. Topic: %s", topic);

    while (1) {
        /* Read runtime-configurable values on every iteration. */
        uint32_t interval_ms = app_runtime_config_get_heartbeat_interval_ms();
        app_runtime_config_get_attribute_name(attribute_name, sizeof(attribute_name));
        app_runtime_config_get_device_status(device_status, sizeof(device_status));

        /* Build ISO-8601 UTC timestamp. */
        time_t now;
        struct tm timeinfo;
        time(&now);
        gmtime_r(&now, &timeinfo);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        /* Read RSSI from WiFi driver. */
        int8_t rssi = wifi_manager_get_rssi();

        /* Serialise payload into the stack buffer. */
        int len = snprintf(json_buf, sizeof(json_buf),
                           "{"
                           "\"serial_no\":\"%s\","
                           "\"attribute_name\":\"%s\","
                           "\"device_status\":\"%s\","
                           "\"time_stamp\":\"%s\","
                           "\"rssi\":%d"
                           "}",
                           APP_DEVICE_ID,
                           attribute_name,
                           device_status,
                           timestamp,
                           (int)rssi);

        if (len > 0 && len < (int)sizeof(json_buf)) {
            mqtt_client_manager_publish(topic, json_buf, APP_MQTT_QOS);
            ESP_LOGI(TAG, "→ %s  %s", topic, json_buf);
        } else {
            ESP_LOGE(TAG, "JSON buffer overflow (need %d, have %d)",
                     len, (int)sizeof(json_buf));
        }

        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
esp_err_t heartbeat_publisher_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        heartbeat_task,
        "heartbeat_task",
        4096,
        NULL,
        5,    /* priority */
        NULL,
        1     /* Core 1 */
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
