// main/ui/ui_theme.h —— 6 套 UI 配色方案
#pragma once

#include "lvgl.h"
#include "../config.h"
#include "../bambu_state.h"

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

// 颜色工具: "RRGGBBAA"/"RRGGBB" hex 字符串 -> lv_color (来自 MQTT tray_color)
lv_color_t ui_theme_hex_color(const char *hex);
// 亮度对比度: 亮背景返黑字, 暗背景返白字 (移植自 BambuHelper 算法)
lv_color_t ui_theme_contrast_text(uint32_t rgb);
// 色块外观按 MQTT 实时数据渲染: 透明料画空心描边, 其余填充真实颜色 (无效时灰色兜底)
void ui_theme_tray_swatch(lv_obj_t *swatch, const bambu_ams_tray_t *t);
