idf_component_register(
    SRCS
        "cmd_handler.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_config
        network
        json
        freertos
        esp_system
        esp_https_ota
        esp_http_client
)
