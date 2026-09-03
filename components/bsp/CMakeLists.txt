idf_component_register(
    SRCS "src/bsp_display.c"
         "src/bsp_display_lvgl.c"
         "src/bsp_button.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_lcd esp_lvgl_port button esp_adc
)
