idf_component_register(
    SRCS
        "heartbeat_publisher.c"
        "counter_publisher.c"
        "log_publisher.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        network
        esp_wifi
        freertos
)
