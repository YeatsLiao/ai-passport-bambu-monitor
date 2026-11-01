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

// 熄屏/唤醒: 熄屏 = 关背光 + 面板进 sleep (0x28); 唤醒 = 面板退出 sleep (0x11)
// + 恢复熄屏前的背光亮度。调用方需持 bsp_lvgl_lock, 避免与 LVGL flush 并发操作 SPI。
void bsp_display_sleep(void);
void bsp_display_wake(void);

// LVGL 接入
struct _lv_display_t;
struct _lv_display_t *bsp_lvgl_init(void);
bool bsp_lvgl_lock(int timeout_ms);
void bsp_lvgl_unlock(void);
