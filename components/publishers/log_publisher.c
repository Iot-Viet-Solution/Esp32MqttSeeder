#include "log_publisher.h"
#include "app_config.h"
#include "app_runtime_config.h"
#include "mqtt_client_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "log_pub";

#define LOG_JSON_BUF_SIZE  384
#define LOG_TOPIC_BUF_SIZE 128

/* ── Publisher task ───────────────────────────────────────────────────────── */
static void log_task(void *pvParameters)
{
    char    topic[LOG_TOPIC_BUF_SIZE];
    char    json_buf[LOG_JSON_BUF_SIZE];
    char    timestamp[25];
    char    log_level[16];
    char    last_log_level[16];

    last_log_level[0] = '\0'; /* force initial topic build */

    ESP_LOGI(TAG, "Log task started");

    while (1) {
        /* Read runtime-configurable values on every iteration. */
        uint32_t interval_ms = app_runtime_config_get_log_interval_ms();
        app_runtime_config_get_log_level(log_level, sizeof(log_level));

        /* Rebuild topic only when log_level changes. */
        if (strcmp(log_level, last_log_level) != 0) {
            snprintf(topic, sizeof(topic), "devices/%s/log/%s",
                     APP_DEVICE_ID, log_level);
            ESP_LOGI(TAG, "Log topic → %s  Interval: %" PRIu32 " ms",
                     topic, interval_ms);
            strlcpy(last_log_level, log_level, sizeof(last_log_level));
        }

        /* ISO-8601 UTC timestamp. */
        time_t now;
        struct tm timeinfo;
        time(&now);
        gmtime_r(&now, &timeinfo);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

        /* Collect system metrics. */
        uint32_t free_heap  = esp_get_free_heap_size();
        uint32_t min_heap   = esp_get_minimum_free_heap_size();
        int      mqtt_queue = mqtt_client_manager_get_outbox_size();
        uint32_t task_count = (uint32_t)uxTaskGetNumberOfTasks();

        int len = snprintf(json_buf, sizeof(json_buf),
                           "{"
                           "\"time_stamp\":\"%s\","
                           "\"free_heap\":%" PRIu32 ","
                           "\"min_heap\":%" PRIu32 ","
                           "\"mqtt_queue\":%d,"
                           "\"task_count\":%" PRIu32
                           "}",
                           timestamp,
                           free_heap,
                           min_heap,
                           mqtt_queue,
                           task_count);

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
esp_err_t log_publisher_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        log_task,
        "log_task",
        4096,
        NULL,
        5,    /* priority */
        NULL,
        1     /* Core 1 */
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log task");
        return ESP_FAIL;
    }
    return ESP_OK;
}
