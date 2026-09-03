// components/bsp/include/bsp_pins.h
// ai-passport-bambu-monitor 硬件引脚定义（与 ai-passport 共用同一块 PCB）
#pragma once

#include "driver/spi_master.h"
#include "driver/i2c_types.h"
#include "hal/adc_types.h"

// ============================================================================
// 显示: ST7789P3 240x320, 4-line SPI
// ============================================================================
#define BSP_LCD_W            240
#define BSP_LCD_H            320
#define BSP_LCD_SPI_HOST     SPI2_HOST
#define BSP_LCD_MOSI         9
#define BSP_LCD_SCLK         8
#define BSP_LCD_CS           1
#define BSP_LCD_DC           20
#define BSP_LCD_RST          (-1)         // 未接 MCU，走 SWRESET 软复位
#define BSP_LCD_BL           21           // 背光, LEDC PWM 调光
#define BSP_LCD_PCLK_HZ      (40 * 1000 * 1000)
#define BSP_LCD_SPI_MODE     0
#define BSP_LCD_INVERT_COLOR 1

// 背光 LEDC 参数
#define BSP_BL_LEDC_TIMER    LEDC_TIMER_0
#define BSP_BL_LEDC_MODE     LEDC_LOW_SPEED_MODE
#define BSP_BL_LEDC_CHANNEL  LEDC_CHANNEL_0
#define BSP_BL_LEDC_RES      LEDC_TIMER_10_BIT
#define BSP_BL_LEDC_FREQ_HZ  5000

// ============================================================================
// 按键: 三键共用一个 ADC 引脚 (GPIO0), 靠分压电阻区分
// ============================================================================
#define BSP_BTN_ADC_UNIT     ADC_UNIT_1
#define BSP_BTN_ADC_CHANNEL  ADC_CHANNEL_0    // GPIO0
#define BSP_BTN_COUNT        3

// 每键的电压窗口 {min_mV, max_mV}
#define BSP_BTN_MV_TABLE  { {0, 150}, {150, 447}, {447, 1900} }
