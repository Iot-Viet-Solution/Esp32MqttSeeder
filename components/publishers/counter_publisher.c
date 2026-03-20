#include "counter_publisher.h"
#include "app_config.h"
#include "mqtt_client_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "counter_pub";

#define COUNTER_JSON_BUF_SIZE  128
#define COUNTER_TOPIC_BUF_SIZE 64

/* ── Publisher task ───────────────────────────────────────────────────────── */
static void counter_task(void *pvParameters)
{
    char    topic[COUNTER_TOPIC_BUF_SIZE];
    char    json_buf[COUNTER_JSON_BUF_SIZE];
    char    timestamp[25];
    uint64_t shoot_count = 0;

    snprintf(topic, sizeof(topic), "uplink/v3/di/%d", APP_COUNTER_ID);

    ESP_LOGI(TAG, "Counter task started. Topic: %s  Interval: %d ms  ID: %d",
             topic, APP_COUNTER_INTERVAL_MS, APP_COUNTER_ID);

    while (1) {
        /* ISO-8601 UTC timestamp. */
        time_t now;
        struct tm timeinfo;
        time(&now);
        gmtime_r(&now, &timeinfo);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        shoot_count++;

        /*
         * pulse_time: the configured interval between publishes (ms).
         * This represents the time between consecutive pulses/shoots.
         */
        int len = snprintf(json_buf, sizeof(json_buf),
                           "{"
                           "\"time_stamp\":\"%s\","
                           "\"shoot_count\":%" PRIu64 ","
                           "\"pulse_time\":%d"
                           "}",
                           timestamp,
                           shoot_count,
                           APP_COUNTER_INTERVAL_MS);

        if (len > 0 && len < (int)sizeof(json_buf)) {
            mqtt_client_manager_publish(topic, json_buf, APP_MQTT_QOS);
            ESP_LOGI(TAG, "→ %s  %s", topic, json_buf);
        } else {
            ESP_LOGE(TAG, "JSON buffer overflow (need %d, have %d)",
                     len, (int)sizeof(json_buf));
        }

        vTaskDelay(pdMS_TO_TICKS(APP_COUNTER_INTERVAL_MS));
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
esp_err_t counter_publisher_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        counter_task,
        "counter_task",
        4096,
        NULL,
        5,    /* priority */
        NULL,
        1     /* Core 1 */
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create counter task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
