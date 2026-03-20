#include "app_runtime_config.h"
#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "app_runtime_cfg";

/* ── Internal state ───────────────────────────────────────────────────────── */
typedef struct {
    uint32_t heartbeat_interval_ms;
    char     attribute_name[64];
    char     device_status[32];

    uint32_t counter_interval_ms;
    int      counter_id;

    uint32_t log_interval_ms;
    char     log_level[16];
} runtime_config_t;

static runtime_config_t  s_cfg;
static SemaphoreHandle_t s_mutex = NULL;

/* ── Helpers ──────────────────────────────────────────────────────────────── */
static inline void lock(void)   { xSemaphoreTake(s_mutex, portMAX_DELAY); }
static inline void unlock(void) { xSemaphoreGive(s_mutex); }

/* ── Public API ───────────────────────────────────────────────────────────── */
esp_err_t app_runtime_config_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create runtime config mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Seed from Kconfig compile-time defaults. */
    s_cfg.heartbeat_interval_ms = APP_HEARTBEAT_INTERVAL_MS;
    strlcpy(s_cfg.attribute_name, APP_ATTRIBUTE_NAME, sizeof(s_cfg.attribute_name));
    strlcpy(s_cfg.device_status,  APP_DEVICE_STATUS,  sizeof(s_cfg.device_status));

    s_cfg.counter_interval_ms = APP_COUNTER_INTERVAL_MS;
    s_cfg.counter_id          = APP_COUNTER_ID;

    s_cfg.log_interval_ms = APP_LOG_INTERVAL_MS;
    strlcpy(s_cfg.log_level, APP_LOG_LEVEL, sizeof(s_cfg.log_level));

    ESP_LOGI(TAG, "Runtime config initialised from Kconfig defaults");
    return ESP_OK;
}

/* ── Heartbeat ────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_heartbeat_interval_ms(void)
{
    lock();
    uint32_t v = s_cfg.heartbeat_interval_ms;
    unlock();
    return v;
}

void app_runtime_config_set_heartbeat_interval_ms(uint32_t ms)
{
    lock();
    s_cfg.heartbeat_interval_ms = ms;
    unlock();
}

void app_runtime_config_get_attribute_name(char *buf, size_t size)
{
    lock();
    strlcpy(buf, s_cfg.attribute_name, size);
    unlock();
}

void app_runtime_config_set_attribute_name(const char *name)
{
    if (!name) {
        ESP_LOGW(TAG, "set_attribute_name: ignoring NULL input");
        return;
    }
    lock();
    strlcpy(s_cfg.attribute_name, name, sizeof(s_cfg.attribute_name));
    unlock();
}

void app_runtime_config_get_device_status(char *buf, size_t size)
{
    lock();
    strlcpy(buf, s_cfg.device_status, size);
    unlock();
}

void app_runtime_config_set_device_status(const char *status)
{
    if (!status) {
        ESP_LOGW(TAG, "set_device_status: ignoring NULL input");
        return;
    }
    lock();
    strlcpy(s_cfg.device_status, status, sizeof(s_cfg.device_status));
    unlock();
}

/* ── Counter ──────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_counter_interval_ms(void)
{
    lock();
    uint32_t v = s_cfg.counter_interval_ms;
    unlock();
    return v;
}

void app_runtime_config_set_counter_interval_ms(uint32_t ms)
{
    lock();
    s_cfg.counter_interval_ms = ms;
    unlock();
}

int app_runtime_config_get_counter_id(void)
{
    lock();
    int v = s_cfg.counter_id;
    unlock();
    return v;
}

void app_runtime_config_set_counter_id(int id)
{
    lock();
    s_cfg.counter_id = id;
    unlock();
}

/* ── Log ──────────────────────────────────────────────────────────────────── */
uint32_t app_runtime_config_get_log_interval_ms(void)
{
    lock();
    uint32_t v = s_cfg.log_interval_ms;
    unlock();
    return v;
}

void app_runtime_config_set_log_interval_ms(uint32_t ms)
{
    lock();
    s_cfg.log_interval_ms = ms;
    unlock();
}

void app_runtime_config_get_log_level(char *buf, size_t size)
{
    lock();
    strlcpy(buf, s_cfg.log_level, size);
    unlock();
}

void app_runtime_config_set_log_level(const char *level)
{
    if (!level) {
        ESP_LOGW(TAG, "set_log_level: ignoring NULL input");
        return;
    }
    lock();
    strlcpy(s_cfg.log_level, level, sizeof(s_cfg.log_level));
    unlock();
}
