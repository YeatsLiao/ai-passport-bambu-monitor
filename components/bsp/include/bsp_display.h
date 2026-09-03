// components/bsp/include/bsp_display.h
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include <stdbool.h>
#include <stdint.h>

// 初始化 SPI 总线、面板、厂商寄存器、背光 LEDC
esp_err_t bsp_display_init(void);
esp_lcd_panel_handle_t bsp_display_panel(void);
esp_lcd_panel_io_handle_t bsp_display_io(void);
void bsp_display_backlight(uint8_t percent);

// LVGL 接入
struct _lv_display_t;
struct _lv_display_t *bsp_lvgl_init(void);
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
