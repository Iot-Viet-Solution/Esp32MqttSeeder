#include "cmd_handler.h"
#include "app_config.h"
#include "app_runtime_config.h"
#include "mqtt_client_manager.h"

#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cmd_handler";

/* Topic buffers sized for: "cmd/" + device_id + "/config/heartbeat" + NUL */
#define TOPIC_BUF_SIZE  128

static char s_topic_heartbeat[TOPIC_BUF_SIZE];
static char s_topic_counter[TOPIC_BUF_SIZE];
static char s_topic_log[TOPIC_BUF_SIZE];
static char s_topic_device[TOPIC_BUF_SIZE];
static char s_topic_reboot[TOPIC_BUF_SIZE];
static char s_topic_ota[TOPIC_BUF_SIZE];

/* ── Per-command handlers ─────────────────────────────────────────────────── */

static void handle_heartbeat_cmd(const cJSON *root)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "interval_ms");
    if (cJSON_IsNumber(item) && item->valuedouble > 0) {
        uint32_t ms = (uint32_t)item->valuedouble;
        app_runtime_config_set_heartbeat_interval_ms(ms);
        ESP_LOGI(TAG, "Heartbeat interval updated → %" PRIu32 " ms", ms);
    } else {
        ESP_LOGW(TAG, "heartbeat cmd: missing or invalid 'interval_ms'");
    }
}

static void handle_counter_cmd(const cJSON *root)
{
    const cJSON *item_ms = cJSON_GetObjectItemCaseSensitive(root, "interval_ms");
    if (cJSON_IsNumber(item_ms) && item_ms->valuedouble > 0) {
        uint32_t ms = (uint32_t)item_ms->valuedouble;
        app_runtime_config_set_counter_interval_ms(ms);
        ESP_LOGI(TAG, "Counter interval updated → %" PRIu32 " ms", ms);
    }

    const cJSON *item_id = cJSON_GetObjectItemCaseSensitive(root, "counter_id");
    if (cJSON_IsNumber(item_id)) {
        int id = (int)item_id->valuedouble;
        app_runtime_config_set_counter_id(id);
        ESP_LOGI(TAG, "Counter ID updated → %d", id);
    }

    const cJSON *item_reset = cJSON_GetObjectItemCaseSensitive(root, "reset_hour");
    if (cJSON_IsNumber(item_reset)) {
        double h = item_reset->valuedouble;
        uint8_t hour = (h >= 0 && h <= 23) ? (uint8_t)h : 255;
        app_runtime_config_set_counter_reset_hour(hour);
        if (hour <= 23) {
            ESP_LOGI(TAG, "Counter reset hour → %02d UTC", (int)hour);
        } else {
            ESP_LOGI(TAG, "Counter reset hour → disabled");
        }
    }
}

static void handle_log_cmd(const cJSON *root)
{
    const cJSON *item_ms = cJSON_GetObjectItemCaseSensitive(root, "interval_ms");
    if (cJSON_IsNumber(item_ms) && item_ms->valuedouble > 0) {
        uint32_t ms = (uint32_t)item_ms->valuedouble;
        app_runtime_config_set_log_interval_ms(ms);
        ESP_LOGI(TAG, "Log interval updated → %" PRIu32 " ms", ms);
    }

    const cJSON *item_level = cJSON_GetObjectItemCaseSensitive(root, "level");
    if (cJSON_IsString(item_level) && item_level->valuestring) {
        app_runtime_config_set_log_level(item_level->valuestring);
        ESP_LOGI(TAG, "Log level updated → %s", item_level->valuestring);
    }
}

static void handle_device_cmd(const cJSON *root)
{
    const cJSON *item_attr = cJSON_GetObjectItemCaseSensitive(root, "attribute_name");
    if (cJSON_IsString(item_attr) && item_attr->valuestring) {
        app_runtime_config_set_attribute_name(item_attr->valuestring);
        ESP_LOGI(TAG, "Attribute name updated → %s", item_attr->valuestring);
    }

    const cJSON *item_status = cJSON_GetObjectItemCaseSensitive(root, "device_status");
    if (cJSON_IsString(item_status) && item_status->valuestring) {
        app_runtime_config_set_device_status(item_status->valuestring);
        ESP_LOGI(TAG, "Device status updated → %s", item_status->valuestring);
    }
}

static void handle_reboot_cmd(void)
{
    ESP_LOGW(TAG, "Reboot command received – rebooting in 1 s...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* ── OTA firmware update ──────────────────────────────────────────────────── */

#define OTA_URL_MAX_LENGTH   256
#define OTA_HTTP_TIMEOUT_MS  5000
#define OTA_TASK_STACK_SIZE  8192

/* Parameters heap-allocated by handle_ota_cmd and freed by ota_update_task. */
typedef struct {
    char url[OTA_URL_MAX_LENGTH];
    char md5[33]; /* 32 hex chars + NUL */
} ota_task_params_t;

static void ota_update_task(void *pvParameters)
{
    ota_task_params_t *params = (ota_task_params_t *)pvParameters;

    ESP_LOGI(TAG, "OTA update started: url=%s md5=%s", params->url, params->md5);

    esp_http_client_config_t http_cfg = {
        .url                    = params->url,
        .timeout_ms             = OTA_HTTP_TIMEOUT_MS,
        .keep_alive_enable      = true,
        /* Allow plain HTTP for development; use a CA cert for production. */
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful – rebooting in 1 s");
        vTaskDelay(pdMS_TO_TICKS(1000));
        free(params);
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        free(params);
    }

    vTaskDelete(NULL);
}

static void handle_ota_cmd(const cJSON *root)
{
    const cJSON *item_url = cJSON_GetObjectItemCaseSensitive(root, "url");
    const cJSON *item_md5 = cJSON_GetObjectItemCaseSensitive(root, "md5");

    if (!cJSON_IsString(item_url) || !item_url->valuestring ||
            item_url->valuestring[0] == '\0') {
        ESP_LOGW(TAG, "OTA cmd: missing or invalid 'url'");
        return;
    }

    if (!cJSON_IsString(item_md5) || !item_md5->valuestring ||
            item_md5->valuestring[0] == '\0') {
        ESP_LOGW(TAG, "OTA cmd: missing or invalid 'md5'");
        return;
    }

    ESP_LOGI(TAG, "OTA update requested: url=%s md5=%s",
             item_url->valuestring, item_md5->valuestring);

    ota_task_params_t *params = malloc(sizeof(ota_task_params_t));
    if (!params) {
        ESP_LOGE(TAG, "OOM – cannot start OTA task");
        return;
    }

    strlcpy(params->url, item_url->valuestring, sizeof(params->url));
    strlcpy(params->md5, item_md5->valuestring, sizeof(params->md5));

    /* OTA runs in its own task to avoid blocking the MQTT event loop. */
    BaseType_t ret = xTaskCreate(ota_update_task, "ota_task",
                                 OTA_TASK_STACK_SIZE, params, 5, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        free(params);
    }
}

/* ── MQTT message callback ────────────────────────────────────────────────── */

static void on_mqtt_message(const char *topic, int topic_len,
                            const char *data,  int data_len)
{
    /* Build a null-terminated copy of the topic for comparison. */
    char topic_str[TOPIC_BUF_SIZE];
    int tlen = (topic_len < (int)(sizeof(topic_str) - 1))
               ? topic_len : (int)(sizeof(topic_str) - 1);
    memcpy(topic_str, topic, tlen);
    topic_str[tlen] = '\0';

    /* Reboot needs no payload. */
    if (strcmp(topic_str, s_topic_reboot) == 0) {
        handle_reboot_cmd();
        return;
    }

    /* Parse JSON payload (data is NOT null-terminated). */
    /* Use a stack buffer for typical small command payloads. */
    char  data_stack[512];
    char *data_str;
    bool  heap_alloc = false;

    if (data_len <= 0 || !data) {
        ESP_LOGW(TAG, "Empty or missing payload on '%s'", topic_str);
        return;
    }

    if (data_len < (int)sizeof(data_stack)) {
        data_str = data_stack;
    } else {
        data_str = malloc((size_t)data_len + 1);
        if (!data_str) {
            ESP_LOGE(TAG, "OOM parsing command payload on '%s'", topic_str);
            return;
        }
        heap_alloc = true;
    }

    memcpy(data_str, data, data_len);
    data_str[data_len] = '\0';

    cJSON *root = cJSON_Parse(data_str);
    if (heap_alloc) {
        free(data_str);
    }

    if (!root) {
        ESP_LOGW(TAG, "Invalid or empty JSON on '%s'", topic_str);
        return;
    }

    if (strcmp(topic_str, s_topic_heartbeat) == 0) {
        handle_heartbeat_cmd(root);
    } else if (strcmp(topic_str, s_topic_counter) == 0) {
        handle_counter_cmd(root);
    } else if (strcmp(topic_str, s_topic_log) == 0) {
        handle_log_cmd(root);
    } else if (strcmp(topic_str, s_topic_device) == 0) {
        handle_device_cmd(root);
    } else if (strcmp(topic_str, s_topic_ota) == 0) {
        handle_ota_cmd(root);
    } else {
        ESP_LOGW(TAG, "Unknown command topic: %s", topic_str);
    }

    cJSON_Delete(root);
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t cmd_handler_init(void)
{
    /* Build command topic strings using the compile-time device ID. */
    snprintf(s_topic_heartbeat, sizeof(s_topic_heartbeat),
             "cmd/%s/config/heartbeat", APP_DEVICE_ID);
    snprintf(s_topic_counter, sizeof(s_topic_counter),
             "cmd/%s/config/counter", APP_DEVICE_ID);
    snprintf(s_topic_log, sizeof(s_topic_log),
             "cmd/%s/config/log", APP_DEVICE_ID);
    snprintf(s_topic_device, sizeof(s_topic_device),
             "cmd/%s/config/device", APP_DEVICE_ID);
    snprintf(s_topic_reboot, sizeof(s_topic_reboot),
             "cmd/%s/reboot", APP_DEVICE_ID);
    snprintf(s_topic_ota, sizeof(s_topic_ota),
             "cmd/%s/ota", APP_DEVICE_ID);

    /* Register the message callback before subscribing. */
    mqtt_client_manager_set_message_handler(on_mqtt_message);

    /* Subscribe to all command topics (stored for auto re-subscribe). */
    const int qos = APP_MQTT_QOS;
    mqtt_client_manager_subscribe(s_topic_heartbeat, qos);
    mqtt_client_manager_subscribe(s_topic_counter,   qos);
    mqtt_client_manager_subscribe(s_topic_log,       qos);
    mqtt_client_manager_subscribe(s_topic_device,    qos);
    mqtt_client_manager_subscribe(s_topic_reboot,    qos);
    mqtt_client_manager_subscribe(s_topic_ota,       qos);

    ESP_LOGI(TAG, "Command handler initialised. Subscribed topics:");
    ESP_LOGI(TAG, "  %s  →  {\"interval_ms\":<ms>}", s_topic_heartbeat);
    ESP_LOGI(TAG, "  %s  →  {\"interval_ms\":<ms>, \"counter_id\":<id>, \"reset_hour\":<0-23 or 255=disabled>}", s_topic_counter);
    ESP_LOGI(TAG, "  %s  →  {\"interval_ms\":<ms>, \"level\":\"<lvl>\"}", s_topic_log);
    ESP_LOGI(TAG, "  %s  →  {\"attribute_name\":\"<n>\", \"device_status\":\"<s>\"}", s_topic_device);
    ESP_LOGI(TAG, "  %s  →  {} (triggers reboot)", s_topic_reboot);
    ESP_LOGI(TAG, "  %s  →  {\"url\":\"<url>\", \"md5\":\"<md5>\"} (OTA update)", s_topic_ota);

    return ESP_OK;
}
