#include "mqtt_client_manager.h"
#include "app_config.h"

#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include "freertos/timers.h"

/* ── Optional TLS certificate references ──────────────────────────────────── */
#ifdef CONFIG_SEEDER_MQTT_TLS_VERIFY_CA
extern const uint8_t mqtt_ca_crt_start[]     asm("_binary_mqtt_ca_crt_start");
extern const uint8_t mqtt_ca_crt_end[]       asm("_binary_mqtt_ca_crt_end");
#endif

#ifdef CONFIG_SEEDER_MQTT_TLS_VERIFY_THUMBPRINT
extern const uint8_t mqtt_broker_crt_start[] asm("_binary_mqtt_broker_crt_start");
extern const uint8_t mqtt_broker_crt_end[]   asm("_binary_mqtt_broker_crt_end");
#endif

#ifdef CONFIG_SEEDER_MQTT_CLIENT_CERT
extern const uint8_t mqtt_client_crt_start[] asm("_binary_mqtt_client_crt_start");
extern const uint8_t mqtt_client_crt_end[]   asm("_binary_mqtt_client_crt_end");
extern const uint8_t mqtt_client_key_start[] asm("_binary_mqtt_client_key_start");
extern const uint8_t mqtt_client_key_end[]   asm("_binary_mqtt_client_key_end");
#endif

static const char *TAG = "mqtt_client_manager";

/* MQTT5 message expiry interval (seconds) for all publishes. */
#define MQTT5_MSG_EXPIRY_INTERVAL_SEC  60U

/* Maximum number of topics that can be subscribed at once. */
#define MAX_SUBSCRIPTIONS  16

/* ── Reconnect backoff ────────────────────────────────────────────────────── */
/* Initial reconnect delay.  Doubles on every failure up to RECONNECT_MAX_MS. */
#define RECONNECT_BASE_MS     2000U
/* Upper bound on reconnect delay (before jitter). */
#define RECONNECT_MAX_MS      60000U
/* Exponent cap so the shift never overflows uint32_t (2^RECONNECT_MAX_EXP). */
#define RECONNECT_MAX_EXP     5U
/* Maximum random jitter added to each delay to avoid thundering-herd. */
#define RECONNECT_JITTER_MS   2000U

/* ── Subscription table ───────────────────────────────────────────────────── */
typedef struct {
    char topic[128];
    int  qos;
} subscription_entry_t;

static subscription_entry_t   s_subscriptions[MAX_SUBSCRIPTIONS];
static int                    s_sub_count         = 0;

/* ── State ────────────────────────────────────────────────────────────────── */
static esp_mqtt_client_handle_t s_client            = NULL;
static atomic_bool               s_connected         = false;
static mqtt_message_handler_t   s_message_handler   = NULL;
static mqtt_connected_handler_t s_connected_handler = NULL;

/* Reconnect state (accessed from the MQTT-event and timer-daemon tasks). */
static TimerHandle_t            s_reconnect_timer   = NULL;
static atomic_uint_least8_t     s_reconnect_attempt = 0;

/* ── Reconnect helpers ────────────────────────────────────────────────────── */

/* Compute exponential-backoff delay with hardware-RNG jitter.
 * delay = min(BASE * 2^attempt, MAX) + random jitter in [0, JITTER_MS). */
static uint32_t reconnect_delay_ms(void)
{
    uint_least8_t attempt = atomic_load(&s_reconnect_attempt);
    uint8_t exp = attempt < RECONNECT_MAX_EXP ? attempt : RECONNECT_MAX_EXP;
    uint32_t delay = RECONNECT_BASE_MS << exp;
    if (delay > RECONNECT_MAX_MS) {
        delay = RECONNECT_MAX_MS;
    }
    uint32_t jitter = esp_random() % RECONNECT_JITTER_MS;
    return delay + jitter;
}

/* FreeRTOS software-timer callback: request one reconnect attempt.
 * Runs in the timer-daemon task; must not block. */
static void reconnect_timer_cb(TimerHandle_t xTimer)
{
    if (atomic_load(&s_connected)) {
        return; /* already reconnected by the time the timer fired */
    }
    ESP_LOGI(TAG, "MQTT reconnect attempt %d", (int)atomic_load(&s_reconnect_attempt));
    esp_err_t err = esp_mqtt_client_reconnect(s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_reconnect() failed (err=0x%x) – "
                      "next DISCONNECTED event will reschedule", err);
    }
}

/* Arm the reconnect timer for the next backoff interval.
 * Computes the delay using the current attempt count, then increments the
 * counter so the next failure uses a longer backoff.  The delay sequence is:
 *   attempt 0 → 2 s, attempt 1 → 4 s, …, attempt ≥5 → 60 s  (+jitter). */
static void schedule_reconnect(void)
{
    uint32_t delay_ms = reconnect_delay_ms();
    ESP_LOGW(TAG, "MQTT reconnect scheduled in %" PRIu32 " ms (attempt %d)",
             delay_ms, (int)atomic_load(&s_reconnect_attempt));
    uint_least8_t cur = atomic_load(&s_reconnect_attempt);
    if (cur < UINT8_MAX) {
        atomic_store(&s_reconnect_attempt, (uint_least8_t)(cur + 1));
    }
    /* xTimerChangePeriod also starts the timer if it was dormant. */
    if (xTimerChangePeriod(s_reconnect_timer, pdMS_TO_TICKS(delay_ms), 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to arm reconnect timer – reconnect will not occur");
    }
}

/* ── Internal helpers ─────────────────────────────────────────────────────── */

/* Re-subscribe to all stored topics after a (re-)connect. */
static void resubscribe_all(void)
{
    for (int i = 0; i < s_sub_count; i++) {
        int msg_id = esp_mqtt_client_subscribe_single(s_client,
                                                      s_subscriptions[i].topic,
                                                      s_subscriptions[i].qos);
        if (msg_id < 0) {
            ESP_LOGW(TAG, "Re-subscribe failed for '%s'", s_subscriptions[i].topic);
        } else {
            ESP_LOGD(TAG, "Re-subscribed → '%s' msg_id=%d",
                     s_subscriptions[i].topic, msg_id);
        }
    }
}

/* ── Internal event handler ───────────────────────────────────────────────── */
static void mqtt5_event_handler(void *handler_args,
                                esp_event_base_t base,
                                int32_t event_id,
                                void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            /* session_present=1: broker has a stored session for this client.
             * session_present=0: broker started a clean session (subscriptions
             * lost).  With clean_session=false this may indicate a session
             * takeover or that the broker evicted the previous session. */
            ESP_LOGI(TAG, "MQTT5 connected to broker (session_present=%d)",
                     event->session_present);
            if (!event->session_present) {
                ESP_LOGW(TAG, "Broker started a clean session – all subscriptions "
                              "lost, will resubscribe");
            }
            /* Stop the reconnect timer and reset backoff before doing anything
             * else.  This prevents a timer that was already queued in the
             * daemon task from firing and sending a duplicate CONNECT after we
             * have just successfully connected (which would cause a Takeover
             * disconnect on the broker).
             * Note: even if xTimerStop() fails (queue full), the timer callback
             * checks s_connected and returns early, so no duplicate CONNECT
             * will be sent. */
            if (xTimerStop(s_reconnect_timer, 0) != pdPASS) {
                ESP_LOGW(TAG, "Failed to stop reconnect timer (queue full)");
            }
            atomic_store(&s_reconnect_attempt, 0);
            atomic_store(&s_connected, true);
            resubscribe_all();
            if (s_connected_handler) {
                s_connected_handler();
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT5 disconnected from broker");
            if (event->error_handle) {
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    ESP_LOGW(TAG, "  reason: connection refused (code %d)",
                             event->error_handle->connect_return_code);
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGW(TAG, "  reason: TCP transport error, errno=%d",
                             event->error_handle->esp_transport_sock_errno);
                }
            }
            atomic_store(&s_connected, false);
            /* Schedule the next reconnect attempt with exponential backoff.
             * Using a single software timer ensures only one CONNECT is
             * ever in-flight at a time, eliminating the Takeover race. */
            schedule_reconnect();
            break;

        case MQTT_EVENT_DATA:
            if (s_message_handler && event->topic_len > 0) {
                /* Skip partial / chunked messages. */
                if (event->data_len < event->total_data_len) {
                    ESP_LOGW(TAG, "Partial message received – skipping");
                    break;
                }
                /* event->data may be NULL and event->data_len may be 0 for
                 * messages with no payload; the handler is responsible for
                 * validating these before use. */
                s_message_handler(event->topic, event->topic_len,
                                  event->data,  event->data_len);
            }
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "Publish acknowledged: msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            if (event->error_handle) {
                ESP_LOGE(TAG, "MQTT5 error: type=%d esp_tls_last_esp_err=0x%x "
                              "esp_tls_stack_err=0x%x errno=%d",
                         event->error_handle->error_type,
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err,
                         event->error_handle->esp_transport_sock_errno);
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGE(TAG, "  TCP transport error – check broker reachability");
                } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                    ESP_LOGE(TAG, "  Connection refused by broker (code %d)",
                             event->error_handle->connect_return_code);
                }
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
    /* Create the reconnect timer (one-shot, inactive until first disconnect). */
    s_reconnect_timer = xTimerCreate("mqtt_reconnect",
                                     pdMS_TO_TICKS(RECONNECT_BASE_MS),
                                     pdFALSE,   /* one-shot */
                                     NULL,
                                     reconnect_timer_cb);
    if (s_reconnect_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create reconnect timer");
        return ESP_ERR_NO_MEM;
    }

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
#ifdef CONFIG_SEEDER_MQTT_TLS_ENABLED
            .verification = {
    #ifdef CONFIG_SEEDER_MQTT_TLS_VERIFY_CA
                .certificate     = (const char *)mqtt_ca_crt_start,
                .certificate_len = (size_t)(mqtt_ca_crt_end - mqtt_ca_crt_start),
    #elif defined(CONFIG_SEEDER_MQTT_TLS_VERIFY_THUMBPRINT)
            /* Certificate pinning against broker leaf cert.
             * ESP-MQTT/ESP-TLS does not expose a direct thumbprint field. */
            .certificate     = (const char *)mqtt_broker_crt_start,
            .certificate_len = (size_t)(mqtt_broker_crt_end - mqtt_broker_crt_start),
    #else /* CONFIG_SEEDER_MQTT_TLS_VERIFY_INSECURE */
                /* No CA cert provided – skip broker certificate verification. */
                .skip_cert_common_name_check = true,
                .use_global_ca_store         = false,
    #endif /* verification mode */
            },
#endif /* CONFIG_SEEDER_MQTT_TLS_ENABLED */
        },
        .credentials = {
            .client_id = client_id,
            .username  = (strlen(APP_MQTT_USERNAME) > 0) ? APP_MQTT_USERNAME : NULL,
            .authentication = {
                .password =
                    (strlen(APP_MQTT_PASSWORD) > 0) ? APP_MQTT_PASSWORD : NULL,
#ifdef CONFIG_SEEDER_MQTT_CLIENT_CERT
                .certificate     = (const char *)mqtt_client_crt_start,
                .certificate_len = (size_t)(mqtt_client_crt_end - mqtt_client_crt_start),
                .key             = (const char *)mqtt_client_key_start,
                .key_len         = (size_t)(mqtt_client_key_end - mqtt_client_key_start),
#endif /* CONFIG_SEEDER_MQTT_CLIENT_CERT */
            },
        },
        .session = {
            .protocol_ver        = MQTT_PROTOCOL_V_5,
            .keepalive           = APP_MQTT_KEEPALIVE_SEC,
            .disable_clean_session = false,
        },
        .network = {
            /* Disable the built-in fixed-interval auto-reconnect.  We manage
             * reconnects ourselves via s_reconnect_timer with exponential
             * backoff to prevent duplicate CONNECT packets (Takeover race). */
            .disable_auto_reconnect = true,
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
    return atomic_load(&s_connected);
}

int mqtt_client_manager_publish(const char *topic, const char *payload, int qos)
{
    if (!atomic_load(&s_connected) || s_client == NULL) {
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

int mqtt_client_manager_subscribe(const char *topic, int qos)
{
    if (!topic) return -1;

    /* Store for automatic re-subscription on reconnect. */
    if (s_sub_count < MAX_SUBSCRIPTIONS) {
        strlcpy(s_subscriptions[s_sub_count].topic, topic,
                sizeof(s_subscriptions[0].topic));
        s_subscriptions[s_sub_count].qos = qos;
        s_sub_count++;
    } else {
        ESP_LOGW(TAG, "Subscription table full – cannot store '%s'", topic);
    }

    if (!atomic_load(&s_connected) || s_client == NULL) {
        /* Will be subscribed on the next MQTT_EVENT_CONNECTED. */
        ESP_LOGD(TAG, "Not connected – subscription for '%s' deferred", topic);
        return 0;
    }

    int msg_id = esp_mqtt_client_subscribe_single(s_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Subscribe failed for topic '%s'", topic);
    } else {
        ESP_LOGI(TAG, "Subscribed → '%s' msg_id=%d", topic, msg_id);
    }
    return msg_id;
}

void mqtt_client_manager_set_message_handler(mqtt_message_handler_t handler)
{
    s_message_handler = handler;
}

void mqtt_client_manager_set_connected_handler(mqtt_connected_handler_t handler)
{
    s_connected_handler = handler;
}

int mqtt_client_manager_get_outbox_size(void)
{
    if (s_client == NULL) {
        return -1;
    }
    return (int)esp_mqtt_client_get_outbox_size(s_client);
}

