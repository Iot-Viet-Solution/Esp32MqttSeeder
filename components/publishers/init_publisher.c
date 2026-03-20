#include "init_publisher.h"
#include "app_config.h"
#include "mqtt_client_manager.h"

#include <stdio.h>
#include <time.h>

#include "esp_log.h"

static const char *TAG = "init_pub";

#define INIT_JSON_BUF_SIZE  256

/* ── Internal publish helper ──────────────────────────────────────────────── */

static void publish_init_message(void)
{
    char json_buf[INIT_JSON_BUF_SIZE];
    char timestamp[25]; /* "YYYY-MM-DDTHH:MM:SSZ" + NUL */

    /* Build ISO-8601 UTC timestamp. */
    time_t now;
    struct tm timeinfo;
    time(&now);
    gmtime_r(&now, &timeinfo);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    int len = snprintf(json_buf, sizeof(json_buf),
                       "{\"time_stamp\":\"%s\",\"device_type\":\"%s\"}",
                       timestamp, APP_DEVICE_TYPE);

    if (len > 0 && len < (int)sizeof(json_buf)) {
        mqtt_client_manager_publish("devices/init", json_buf, APP_MQTT_QOS);
        ESP_LOGI(TAG, "→ devices/init  %s", json_buf);
    } else {
        ESP_LOGE(TAG, "JSON buffer overflow (need %d, have %d)",
                 len, (int)sizeof(json_buf));
    }
}

/* ── Connected callback (fires on every broker reconnect) ─────────────────── */

static void on_mqtt_connected(void)
{
    publish_init_message();
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t init_publisher_start(void)
{
    /* Register for future reconnects. */
    mqtt_client_manager_set_connected_handler(on_mqtt_connected);

    /* Publish immediately if the broker connection is already up (the initial
     * MQTT_EVENT_CONNECTED fired before this function was called). */
    if (mqtt_client_manager_is_connected()) {
        publish_init_message();
    }

    ESP_LOGI(TAG, "Init publisher registered. Publishes to devices/init on connect.");
    return ESP_OK;
}
