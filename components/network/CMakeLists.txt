idf_component_register(
    SRCS
        "wifi_manager.c"
        "mqtt_client_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        esp_wifi
        esp_netif
        esp_event
        mqtt
)
