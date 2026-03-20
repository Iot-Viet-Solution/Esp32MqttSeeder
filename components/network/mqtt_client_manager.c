#include "mqtt_client_manager.h"
#include "app_config.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"

static const char *TAG = "mqtt_client_manager";

/* MQTT5 message expiry interval (seconds) for all publishes. */
#define MQTT5_MSG_EXPIRY_INTERVAL_SEC  60U

static esp_mqtt_client_handle_t s_client     = NULL;
static volatile bool            s_connected  = false;

/* ── Internal event handler ───────────────────────────────────────────────── */
static void mqtt5_event_handler(void *handler_args,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT5 connected to broker");
            s_connected = true;
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT5 disconnected from broker");
            s_connected = false;
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Publish acknowledged: msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            if (event->error_handle) {
                ESP_LOGE(TAG, "MQTT5 error: type=%d esp_tls_last_esp_err=0x%x",
                         event->error_handle->error_type,
                         event->error_handle->esp_tls_last_esp_err);
            }
            break;

        default:
            ESP_LOGD(TAG, "Unhandled MQTT5 event id: %" PRId32, event_id);
            break;
    }
}

/* ── Public API ───────────────────────────────────────────────────────────── */
esp_err_t mqtt_client_manager_init(void)
{
    /* Derive client ID from Wi-Fi STA MAC address (12 hex digits, no colons). */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));

    static char client_id[13]; /* 12 hex chars + NUL */
    snprintf(client_id, sizeof(client_id),
             "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "MQTT5 client ID (MAC): %s", client_id);

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = APP_MQTT_BROKER_URI,
        },
        .credentials = {
            .client_id = client_id,
            .username  = (strlen(APP_MQTT_USERNAME) > 0) ? APP_MQTT_USERNAME : NULL,
            .authentication.password =
                (strlen(APP_MQTT_PASSWORD) > 0) ? APP_MQTT_PASSWORD : NULL,
        },
        .session = {
            .protocol_ver        = MQTT_PROTOCOL_V_5,
            .keepalive           = APP_MQTT_KEEPALIVE_SEC,
            .disable_clean_session = false,
        },
        .network = {
            .reconnect_timeout_ms = 5000,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialise MQTT client");
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client,
                                                   ESP_EVENT_ANY_ID,
                                                   mqtt5_event_handler,
                                                   NULL));

    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));

    ESP_LOGI(TAG, "MQTT5 client started → %s", APP_MQTT_BROKER_URI);
    return ESP_OK;
}

esp_mqtt_client_handle_t mqtt_client_manager_get_client(void)
{
    return s_client;
}

bool mqtt_client_manager_is_connected(void)
{
    return s_connected;
}

int mqtt_client_manager_publish(const char *topic, const char *payload, int qos)
{
    if (!s_connected || s_client == NULL) {
        ESP_LOGW(TAG, "Not connected – skipping publish to '%s'", topic);
        return -1;
    }

    /* Set MQTT5 publish properties: JSON content-type + UTF-8 payload format. */
    esp_mqtt5_publish_property_config_t pub_prop = {
        .payload_format_indicator = 1, /* UTF-8 encoded payload */
        .content_type             = "application/json",
        .message_expiry_interval  = MQTT5_MSG_EXPIRY_INTERVAL_SEC,
    };
    esp_mqtt5_client_set_publish_property(s_client, &pub_prop);

    int msg_id = esp_mqtt_client_publish(s_client,
                                         topic,
                                         payload,
                                         payload ? (int)strlen(payload) : 0,
                                         qos,
                                         0 /* retain = false */);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Publish failed to topic '%s'", topic);
    } else {
        ESP_LOGD(TAG, "Published → '%s' msg_id=%d", topic, msg_id);
    }
    return msg_id;
}
