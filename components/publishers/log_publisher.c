#include "log_publisher.h"
#include "app_config.h"
#include "app_runtime_config.h"
#include "mqtt_client_manager.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "log_pub";

#define LOG_JSON_BUF_SIZE  256
#define LOG_TOPIC_BUF_SIZE 128

/* Sample log messages cycled through each publish. */
static const char *const LOG_MESSAGES[] = {
    "Seeder is running normally",
    "MQTT5 connection stable",
    "Counter incremented successfully",
    "Heartbeat published",
    "System health check OK",
};
#define LOG_MESSAGES_COUNT  (sizeof(LOG_MESSAGES) / sizeof(LOG_MESSAGES[0]))

/* ── Publisher task ───────────────────────────────────────────────────────── */
static void log_task(void *pvParameters)
{
    char    topic[LOG_TOPIC_BUF_SIZE];
    char    json_buf[LOG_JSON_BUF_SIZE];
    char    timestamp[25];
    char    log_level[16];
    char    last_log_level[16];
    uint32_t cycle = 0;

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

        const char *message = LOG_MESSAGES[cycle % LOG_MESSAGES_COUNT];
        cycle = (cycle + 1) % LOG_MESSAGES_COUNT;

        int len = snprintf(json_buf, sizeof(json_buf),
                           "{"
                           "\"message\":\"%s\","
                           "\"time_stamp\":\"%s\""
                           "}",
                           message,
                           timestamp);

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
