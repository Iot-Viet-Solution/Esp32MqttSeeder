#include "heartbeat_publisher.h"
#include "app_config.h"
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

    snprintf(topic, sizeof(topic), "uplink/heartbeat/v1/%s", APP_DEVICE_ID);

    ESP_LOGI(TAG, "Heartbeat task started. Topic: %s  Interval: %d ms",
             topic, APP_HEARTBEAT_INTERVAL_MS);

    while (1) {
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
                           APP_ATTRIBUTE_NAME,
                           APP_DEVICE_STATUS,
                           timestamp,
                           (int)rssi);

        if (len > 0 && len < (int)sizeof(json_buf)) {
            mqtt_client_manager_publish(topic, json_buf, APP_MQTT_QOS);
            ESP_LOGI(TAG, "→ %s  %s", topic, json_buf);
        } else {
            ESP_LOGE(TAG, "JSON buffer overflow (need %d, have %d)",
                     len, (int)sizeof(json_buf));
        }

        vTaskDelay(pdMS_TO_TICKS(APP_HEARTBEAT_INTERVAL_MS));
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
