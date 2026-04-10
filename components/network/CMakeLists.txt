set(EMBED_FILES "")
if(CONFIG_SEEDER_MQTT_CA_CERT)
    list(APPEND EMBED_FILES "${CMAKE_SOURCE_DIR}/certs/mqtt_ca.crt")
endif()
if(CONFIG_SEEDER_MQTT_CLIENT_CERT)
    list(APPEND EMBED_FILES
        "${CMAKE_SOURCE_DIR}/certs/mqtt_client.crt"
        "${CMAKE_SOURCE_DIR}/certs/mqtt_client.key"
    )
endif()

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
    EMBED_TXTFILES ${EMBED_FILES}
)
