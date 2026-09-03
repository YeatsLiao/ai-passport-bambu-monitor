// main/ui/ui_theme.h —— 6 套 UI 配色方案
#pragma once

#include "lvgl.h"
#include "../config.h"

typedef struct {
    uint32_t bg;            // 背景色
    uint32_t card_bg;       // 卡片/面板背景色
    uint32_t header_bg;     // 标题栏背景色
    uint32_t footer_bg;     // 底部栏背景色
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
    int      radius;        // 圆角大小
} ui_theme_colors_t;

const ui_theme_colors_t *ui_theme_get_colors(void);
const char *ui_theme_style_name(void);
