idf_component_register(
    SRCS "main.c"
         "bambu_state.c"
         "bambu_mqtt.c"
         "ui/ui_theme.c"
         "ui/ui_monitor.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_wifi esp_timer
             mqtt nvs_flash json
)
