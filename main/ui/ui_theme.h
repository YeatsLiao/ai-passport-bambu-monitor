// main/ui/ui_theme.h —— 颜色主题系统
#pragma once

#include "lvgl.h"
#include "config.h"

// 主题颜色定义
typedef struct {
    uint32_t bg;            // 背景色
    uint32_t card_bg;       // 卡片/面板背景色
    uint32_t text_primary;  // 主要文字
    uint32_t text_secondary;// 次要文字
    uint32_t accent;        // 强调色（进度条等）
    uint32_t success;       // 成功/正常色
    uint32_t warning;       // 警告色
    uint32_t error;         // 错误色
    uint32_t border;        // 边框色
    uint32_t gauge_nozzle;  // 喷嘴温度色
    uint32_t gauge_bed;     // 热床温度色
    uint32_t gauge_chamber; // 腔体温度色
} ui_theme_colors_t;

// 获取当前主题颜色
const ui_theme_colors_t *ui_theme_get_colors(void);

// 获取主题名称
const char *ui_theme_name(void);

// 判断是否为可爱风格（影响圆角、装饰等）
bool ui_theme_is_cute(void);

// 判断是否为深色背景
bool ui_theme_is_dark(void);
