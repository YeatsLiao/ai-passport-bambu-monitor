idf_component_register(
    SRCS "main.c"
         "bambu_state.c"
         "bambu_mqtt.c"
         "ui/ui_theme.c"
         "ui/ui_monitor.c"
         "ui/fonts/lv_font_cn_14.c"
         "ui/fonts/lv_font_cn_20.c"
         "ui/style_bambu.c"
         "ui/style_cyber.c"
         "ui/style_sheikah.c"
         "ui/style_white.c"
         "ui/style_industrial.c"
         "ui/style_neon.c"
         "ui/style_pixel.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_hw_support esp_netif esp_wifi esp_timer
             mqtt nvs_flash json lwip
)
