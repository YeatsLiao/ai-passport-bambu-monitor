// main/ui/ui_theme.h —— 7 套 UI 配色方案
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
// 同上, 但返回 0xRRGGBB, 供按 uint32_t 传色的接口 (如各风格的 mk_lbl) 直接使用
uint32_t ui_theme_on_color(uint32_t bg);
// 色块外观按 MQTT 实时数据渲染: 透明料画空心描边, 其余填充真实颜色 (无效时灰色兜底)
void ui_theme_tray_swatch(lv_obj_t *swatch, const bambu_ams_tray_t *t);

// 图标工具 (LVGL 内置 FontAwesome 符号字形)
//
// 中文字体 (lv_font_cn_*) 的 .fallback 指向同字号 Montserrat, 而 LVGL 9 的
// lv_font_get_glyph_dsc() 是逐字形沿 fallback 链查找的, 因此用 L_FONT_TEXT 渲染
// "图标 + 中文" 混排文本时, 图标会自动回落到 Montserrat, 不会丢字形。
//
// 打印状态 -> 动态图标 (播放/暂停/警告/对勾/电源)
const char *ui_theme_state_icon(bambu_print_state_t s, bool connected);
// 电量百分比 -> 电池图标 (四档)
const char *ui_theme_battery_icon(int soc);
// 电量百分比 -> 电池配色 (随实时数据分档: 无数据灰 / <20 红 / <50 黄 / 其余绿)
uint32_t ui_theme_battery_color(int soc);
// 组件编号 -> 固定图标 (编号见 config.h: 1喷嘴 2热床 3腔体 4层数 5进度 6剩余 7状态 8速度 9AMS)
const char *ui_theme_component_icon(int cmp);
