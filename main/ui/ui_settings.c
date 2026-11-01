// main/ui/ui_settings.c —— 设备设置页
//
// 全屏覆盖层, 独立于 12 套风格 (用 ui_theme 配色, 任一风格下观感一致):
//   亮度    OK 进入编辑, ↑/↓ 调节 (实时预览), OK 保存
//   熄屏    OK 进入编辑, ↑/↓ 档位切换, OK 保存
//   重置网络 OK 两次确认后进入配网模式 (替代原"长按 OK")
//   返回    OK 回监控页; 任意时刻 LONG OK 也可退出
//
// 进入方式: 监控最后一页再按 ↓ (ui_monitor_key 路由)。
// 亮度/熄屏改动通过 app_config_set_display 落 NVS, 重启保留。

#include "ui_settings.h"
#include "ui_lang.h"
#include "ui_theme.h"
#include "ui_monitor.h"
#include "../app_config.h"
#include "bsp_display.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "ui_settings";

// 行定义
enum { ROW_BRIGHT = 0, ROW_SLEEP, ROW_RESET, ROW_BACK, ROW_COUNT };

// 熄屏档位 (分钟), 0=关
static const uint8_t k_sleep_preset[] = {0, 1, 3, 5, 10, 30};
#define SLEEP_PRESET_COUNT  (sizeof(k_sleep_preset) / sizeof(k_sleep_preset[0]))

static bool     s_open = false;
static lv_obj_t *s_panel = NULL;
static lv_obj_t *s_row_obj[ROW_COUNT];
static lv_obj_t *s_val_lbl[ROW_COUNT];
static int      s_row = 0;          // 当前选中行
static bool     s_edit = false;     // 编辑模式 (亮度/熄屏)
static bool     s_reset_armed = false;  // 重置网络两步确认
static uint8_t  s_bright;           // 编辑中的亮度值
static int      s_sleep_idx;        // 编辑中的熄屏档位
static void   (*s_prov_cb)(void) = NULL;

bool ui_settings_is_open(void) { return s_open; }

void ui_settings_set_prov_cb(void (*cb)(void)) { s_prov_cb = cb; }

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
// 把一行画出来 (选中高亮 / 编辑态值加箭头变色)
static void draw_row(int i) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    bool sel = (i == s_row);

    lv_obj_set_style_border_color(s_row_obj[i], lv_color_hex(sel ? c->accent : c->border), 0);
    lv_obj_set_style_border_width(s_row_obj[i], sel ? 2 : 1, 0);
    lv_obj_set_style_bg_color(s_row_obj[i], lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_bg_opa(s_row_obj[i], sel ? LV_OPA_COVER : LV_OPA_70, 0);

    lv_obj_t *val = s_val_lbl[i];
    switch (i) {
        case ROW_BRIGHT:
            if (sel && s_edit)
                lv_label_set_text_fmt(val, LV_SYMBOL_LEFT " %d%% " LV_SYMBOL_RIGHT, s_bright);
            else
                lv_label_set_text_fmt(val, "%d%%", s_bright);
            break;
        case ROW_SLEEP: {
            uint8_t m = k_sleep_preset[s_sleep_idx];
            if (m == 0)
                lv_label_set_text_fmt(val, "%s%s%s",
                                      sel && s_edit ? LV_SYMBOL_LEFT " " : "",
                                      L_OFF,
                                      sel && s_edit ? " " LV_SYMBOL_RIGHT : "");
            else
                lv_label_set_text_fmt(val, "%s%d %s%s",
                                      sel && s_edit ? LV_SYMBOL_LEFT " " : "",
                                      m, L_MIN,
                                      sel && s_edit ? " " LV_SYMBOL_RIGHT : "");
            break;
        }
        case ROW_RESET:
            lv_label_set_text(val, s_reset_armed ? L_CONFIRM_RESET : LV_SYMBOL_RIGHT " ");
            break;
        case ROW_BACK:
            lv_label_set_text(val, LV_SYMBOL_RIGHT " ");
            break;
    }
    // 编辑中的值用强调色, 重置确认提示用警告色
    if (i == ROW_RESET && s_reset_armed)
        lv_obj_set_style_text_color(val, lv_color_hex(c->warning), 0);
    else if (sel && s_edit)
        lv_obj_set_style_text_color(val, lv_color_hex(c->accent), 0);
    else
        lv_obj_set_style_text_color(val, lv_color_hex(c->text_secondary), 0);
}

static void draw_all(void) {
    for (int i = 0; i < ROW_COUNT; i++) draw_row(i);
}

// 熄屏值落在非档位上 (如编译期配了 7 分钟) 时, 回落到不大于它的档位
static int sleep_idx_for(uint8_t minutes) {
    int idx = 0;
    for (size_t i = 0; i < SLEEP_PRESET_COUNT; i++) {
        if (k_sleep_preset[i] <= minutes) idx = (int)i;
    }
    return idx;
}

void ui_settings_open(void) {
    if (s_open || !s_scr) return;

    const ui_theme_colors_t *c = ui_theme_get_colors();
    const app_config_t *cfg = app_config_get();
    s_row = 0;
    s_edit = false;
    s_reset_armed = false;
    s_bright = cfg->backlight;
    s_sleep_idx = sleep_idx_for(cfg->screen_off_min);

    s_panel = lv_obj_create(s_scr);
    if (!s_panel) return;
    lv_obj_set_pos(s_panel, 0, 0);
    lv_obj_set_size(s_panel, 240, 320);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(c->bg), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 0, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(s_panel);

    // 标题
    lv_obj_t *title = lv_label_create(s_panel);
    if (title) {
        lv_label_set_text_fmt(title, "%s %s", LV_SYMBOL_SETTINGS, L_SETTINGS);
        lv_obj_set_style_text_font(title, L_FONT_TEXT_BIG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    }

    // 四行卡片
    const char *row_name[ROW_COUNT] = {L_BRIGHTNESS, L_SLEEP, L_RESET_NET, L_BACK};
    for (int i = 0; i < ROW_COUNT; i++) {
        lv_obj_t *r = lv_obj_create(s_panel);
        if (!r) break;
        s_row_obj[i] = r;
        lv_obj_set_pos(r, 16, 58 + i * 58);
        lv_obj_set_size(r, 208, 50);
        lv_obj_set_style_radius(r, c->radius > 0 ? c->radius : 10, 0);
        lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = lv_label_create(r);
        if (name) {
            lv_label_set_text(name, row_name[i]);
            lv_obj_set_style_text_font(name, L_FONT_TEXT, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(c->text_primary), 0);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, 0);
        }

        lv_obj_t *val = lv_label_create(r);
        if (val) {
            s_val_lbl[i] = val;
            lv_label_set_text(val, "");
            lv_obj_set_style_text_font(val, L_FONT_TEXT, 0);   // 中文 + 符号混排
            lv_obj_align(val, LV_ALIGN_RIGHT_MID, -14, 0);
        }
    }

    // 底部操作提示
    lv_obj_t *hint = lv_label_create(s_panel);
    if (hint) {
        lv_label_set_text(hint,
            LV_SYMBOL_OK " 选择/保存   " LV_SYMBOL_UP LV_SYMBOL_DOWN " 调整/选择");
        lv_obj_set_style_text_font(hint, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(c->text_secondary), 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    s_open = true;
    draw_all();
    ESP_LOGI(TAG, "设置页已打开");
}

static void close_settings(void) {
    if (!s_open) return;
    if (s_panel) {
        lv_obj_delete(s_panel);
        s_panel = NULL;
    }
    for (int i = 0; i < ROW_COUNT; i++) { s_row_obj[i] = NULL; s_val_lbl[i] = NULL; }
    s_open = false;
    s_edit = false;
    s_reset_armed = false;
    // 编辑中途退出: 撤销未保存的亮度预览, 回到已保存值
    bsp_display_backlight(app_config_get()->backlight);
    ESP_LOGI(TAG, "设置页已关闭");
}

// ---------------------------------------------------------------------------
// 按键
// ---------------------------------------------------------------------------
static void adjust(int dir) {
    if (s_row == ROW_BRIGHT) {
        int v = (int)s_bright + dir * 10;
        if (v < 10) v = 100;        // 循环, 方便从 10 调到 100
        if (v > 100) v = 10;
        s_bright = (uint8_t)v;
        bsp_display_backlight(s_bright);   // 实时预览
    } else if (s_row == ROW_SLEEP) {
        int idx = s_sleep_idx + dir;
        if (idx < 0) idx = (int)SLEEP_PRESET_COUNT - 1;
        if (idx >= (int)SLEEP_PRESET_COUNT) idx = 0;
        s_sleep_idx = idx;
    }
    draw_all();
}

// OK 确认: 保存当前行编辑值到 NVS + 运行时单例
static void save_current_row(void) {
    const app_config_t *cfg = app_config_get();
    if (s_row == ROW_BRIGHT) {
        app_config_set_display(s_bright, cfg->screen_off_min);
        ESP_LOGI(TAG, "亮度已保存: %d%%", s_bright);
    } else if (s_row == ROW_SLEEP) {
        app_config_set_display(cfg->backlight, k_sleep_preset[s_sleep_idx]);
        ESP_LOGI(TAG, "熄屏已保存: %d 分钟", k_sleep_preset[s_sleep_idx]);
    }
}

void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!s_open) return;

    // 长按 OK: 编辑中=取消编辑, 否则返回监控页
    if (ev == BSP_BTN_LONG) {
        if (s_edit) { s_edit = false; draw_all(); }
        else close_settings();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;

    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (s_edit) {
            adjust(btn == BSP_BTN_UP ? -1 : +1);
        } else {
            s_reset_armed = false;   // 移动选中即撤销重置确认
            s_row = (s_row + (btn == BSP_BTN_DOWN ? 1 : ROW_COUNT - 1)) % ROW_COUNT;
            draw_all();
        }
        return;
    }

    // OK
    if (s_edit) {
        save_current_row();
        s_edit = false;
        draw_all();
        return;
    }
    switch (s_row) {
        case ROW_BRIGHT:
        case ROW_SLEEP:
            s_edit = true;
            draw_all();
            break;
        case ROW_RESET:
            // 两步确认, 防误触进配网
            if (!s_reset_armed) {
                s_reset_armed = true;
                draw_row(ROW_RESET);
            } else {
                close_settings();
                if (s_prov_cb) s_prov_cb();
            }
            break;
        case ROW_BACK:
            close_settings();
            break;
    }
}
