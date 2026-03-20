#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "wifi_manager.h"
#include "mqtt_client_manager.h"
#include "heartbeat_publisher.h"
#include "counter_publisher.h"
#include "log_publisher.h"

static const char *TAG = "main";

/* Wait for SNTP synchronisation (blocks up to ~30 s). */
static void sync_time_via_ntp(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Waiting for NTP time sync... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        gmtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year >= (2020 - 1900)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        ESP_LOGI(TAG, "Time synchronised: %s", buf);
    } else {
        ESP_LOGW(TAG, "NTP sync timed out – timestamps may be inaccurate");
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Esp32MqttSeeder starting ===");

    /* ── 1. Non-volatile storage ──────────────────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* ── 2. WiFi (blocks until connected or max retries) ─────────────────── */
    ESP_ERROR_CHECK(wifi_manager_init());

    /* ── 3. NTP time sync ────────────────────────────────────────────────── */
    sync_time_via_ntp();

    /* ── 4. MQTT5 client ─────────────────────────────────────────────────── */
    ESP_ERROR_CHECK(mqtt_client_manager_init());

    /* Wait up to 15 s for the broker connection. */
    int mqtt_wait = 0;
    while (!mqtt_client_manager_is_connected() && mqtt_wait < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        mqtt_wait++;
    }

    if (!mqtt_client_manager_is_connected()) {
        ESP_LOGE(TAG, "MQTT broker connection timed out – halting");
        return;
    }

    /* ── 5. Start publisher tasks (all pinned to Core 1) ─────────────────── */
    ESP_ERROR_CHECK(heartbeat_publisher_start());
    ESP_ERROR_CHECK(counter_publisher_start());
    ESP_ERROR_CHECK(log_publisher_start());

    ESP_LOGI(TAG, "All publishers started. Seeder is running.");
    /* app_main may return; the FreeRTOS scheduler keeps tasks alive. */
}
