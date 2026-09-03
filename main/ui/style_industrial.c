// main/ui/style_industrial.c —— 风格5: 硬核机房工控风
// 布局: Page0 = LED 段式进度条 (借鉴 BambuHelper drawLedProgressBar) + 大字百分比
//            + 数据行网格 (每行前置 LED 状态灯: 绿=正常/黄=加热中)
//       Page1 = AMS 列表 + 每槽余量横条
// 翻页策略: 两页卡片同时创建, 翻页只切换显示/隐藏 (不 destroy/rebuild)
#include "ui_monitor.h"
#include "ui_theme.h"
#include "ui_lang.h"
#include "../bambu_state.h"
#include "../bambu_mqtt.h"
#include "../config.h"
#include "bsp_battery.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG __attribute__((unused)) = "style_industrial";

#define HAS_AMS  1
#define LED_SEG_COUNT  20          // LED 进度条段数

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;

static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0: LED 段式进度条 + 大字百分比 + 数据行
static lv_obj_t *s_led_seg[LED_SEG_COUNT];   // LED 段
static lv_obj_t *s_pct_lbl  = NULL;          // 大字百分比
static lv_obj_t *s_remain_lbl = NULL;        // 剩余时间 (百分比右侧)
static lv_obj_t *s_row_dot[5];               // 数据行 LED 灯 (NOZ/BED/CHM/LAYER/STATE)
static lv_obj_t *s_row_lbl[5];               // 数据行文字

// Page 1: AMS 列表 (0-3: AMS 槽, 4: Ext 外挂)
static lv_obj_t *s_ams_chip[5];              // 颜色片
static lv_obj_t *s_ams_lbl[5];               // 类型+余量文字
static lv_obj_t *s_ams_bar[5];               // 余量横条

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_bat_lbl  = NULL;

// 底部栏页码标签
static lv_obj_t *s_pg_lbl = NULL;

// ---------------------------------------------------------------------------
// 工具函数
// ---------------------------------------------------------------------------
static lv_obj_t *mk_lbl(lv_obj_t *parent, const char *text,
                         const lv_font_t *font, uint32_t color) {
    if (!parent) return NULL;
    lv_obj_t *l = lv_label_create(parent);
    if (!l) return NULL;
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

// LED 状态灯 (小圆点, 默认熄灭色)
static lv_obj_t *mk_led(lv_obj_t *parent, int x, int y) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *d = lv_obj_create(parent);
    if (!d) return NULL;
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, 8, 8);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(c->border), 0);
    return d;
}

static const char *state_text(bambu_print_state_t s) {
    switch (s) {
        case BAMBU_STATE_RUNNING: return L_STATE_RUNNING;
        case BAMBU_STATE_PAUSE:   return L_STATE_PAUSE;
        case BAMBU_STATE_FINISH:  return L_STATE_FINISH;
        case BAMBU_STATE_FAILED:  return L_STATE_FAILED;
        case BAMBU_STATE_PREPARE: return L_STATE_PREPARE;
        default:                  return L_STATE_IDLE;
    }
}

static uint32_t state_color(bambu_print_state_t s, const ui_theme_colors_t *c) {
    if (s == BAMBU_STATE_PAUSE)   return c->warning;
    if (s == BAMBU_STATE_FAILED)  return c->error;
    if (s == BAMBU_STATE_RUNNING) return c->success;
    return c->text_secondary;
}

// 加热状态灯色: 目标温度>0 且当前低于目标 5°C 以上 = 黄色(加热中), 否则绿色
static uint32_t heat_led_color(float cur, float target, const ui_theme_colors_t *c) {
    if (target > 0.0f && cur < target - 5.0f) return c->warning;
    if (target > 0.0f) return c->success;
    return c->text_secondary;
}

// tray_color "RRGGBBAA" -> uint32_t RGB
static uint32_t tray_rgb(const char *hex) {
    uint32_t rgb = 0x888888;
    if (hex && strlen(hex) >= 6) {
        char buf[7] = {0};
        memcpy(buf, hex, 6);
        rgb = (uint32_t)strtoul(buf, NULL, 16);
    }
    return rgb;
}

// ---------------------------------------------------------------------------
// Page 0: LED 段式进度条 + 大字百分比 + 数据行网格
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 2, 0);                 // 工控风: 直角微圆
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── LED 段式进度条 (20 段, 9x14, 间隔 1px) ──
    memset(s_led_seg, 0, sizeof(s_led_seg));
    for (int i = 0; i < LED_SEG_COUNT; i++) {
        lv_obj_t *seg = lv_obj_create(card);
        if (!seg) continue;
        lv_obj_set_pos(seg, i * 10, 4);
        lv_obj_set_size(seg, 9, 14);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(seg, 1, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_bg_color(seg, lv_color_hex(c->border), 0);   // 熄灭
        s_led_seg[i] = seg;
    }

    // ── 大字百分比 (居中) + 剩余时间 (右侧) ──
    s_pct_lbl = mk_lbl(card, "--%", &lv_font_montserrat_48, c->success);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, -20, 26);
    s_remain_lbl = mk_lbl(card, "--", &lv_font_montserrat_14, c->text_secondary);
    if (s_remain_lbl) lv_obj_align(s_remain_lbl, LV_ALIGN_TOP_RIGHT, -4, 44);

    // ── 数据行网格: NOZ / BED / CHM / LAYER / STATE ──
    memset(s_row_dot, 0, sizeof(s_row_dot));
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    const char *titles[5] = {L_NOZZLE, L_BED, L_CHAMBER, L_LAYER, L_STATE};
    const char *inits[5]  = {"--/--°C", "--/--°C", "--°C", "--/--", L_CONNECTING};
    for (int i = 0; i < 5; i++) {
        s_row_dot[i] = mk_led(card, 2, 96 + i * 24);
        char buf[48];
        snprintf(buf, sizeof(buf), "%-4s %s", titles[i], inits[i]);
        // 标题用次要色, 值用主色 —— 简化为单标签
        s_row_lbl[i] = mk_lbl(card, buf, &lv_font_montserrat_14, c->text_primary);
        if (s_row_lbl[i]) lv_obj_set_pos(s_row_lbl[i], 16, 92 + i * 24);
    }
}

// ---------------------------------------------------------------------------
// Page 1: AMS 列表 + 每槽余量横条
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, L_AMS, &lv_font_montserrat_20, c->accent);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));

    for (int i = 0; i < 5; i++) {
        int y = 32 + i * 38;
        // 颜色片
        lv_obj_t *chip = lv_obj_create(card);
        if (chip) {
            lv_obj_set_pos(chip, 0, y + 2);
            lv_obj_set_size(chip, 14, 14);
            lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(chip, 2, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->border), 0);
            lv_obj_set_style_bg_color(chip, lv_color_hex(0x888888), 0);
        }
        s_ams_chip[i] = chip;

        // 文字: "#1 PLA 54%"
        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "Ext %s", L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, &lv_font_montserrat_14, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 20, y);

        // 余量横条 (背景轨道 + lv_bar)
        lv_obj_t *bar_bg = lv_obj_create(card);
        if (bar_bg) {
            lv_obj_set_pos(bar_bg, 20, y + 18);
            lv_obj_set_size(bar_bg, 180, 5);
            lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(bar_bg, 2, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            if (bar) {
                lv_obj_set_size(bar, 180, 5);
                lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_set_style_radius(bar, 2, 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar, lv_color_hex(c->success), LV_PART_INDICATOR);
                lv_bar_set_range(bar, 0, 100);
                lv_bar_set_value(bar, 0, LV_ANIM_OFF);
                s_ams_bar[i] = bar;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_industrial_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    if (s_card[0] || s_card[1]) return;   // 已创建过, 跳过

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 (持久) ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, &lv_font_montserrat_14, 0xFFFFFF);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", &lv_font_montserrat_14, 0xFFFFFF);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    s_bat_lbl = mk_lbl(header, "BAT:--", &lv_font_montserrat_14, 0xFFFFFF);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (持久) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, &lv_font_montserrat_14, 0xFFFFFF);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, &lv_font_montserrat_14, 0xFFFFFF);
    if (s_pg_lbl) lv_obj_align(s_pg_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    // ── 内容区域容器 ──
    s_content_area = lv_obj_create(s_scr);
    lv_obj_set_pos(s_content_area, 0, 0);
    lv_obj_set_size(s_content_area, 240, 320);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content_area, 0, 0);
    lv_obj_set_style_pad_all(s_content_area, 0, 0);
    lv_obj_remove_flag(s_content_area, LV_OBJ_FLAG_SCROLLABLE);

    memset(s_card, 0, sizeof(s_card));
    build_page0();
    build_page1();

    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == s_page) lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// 更新 (每秒调用)
// ---------------------------------------------------------------------------
void style_industrial_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[48];

    // 时间
    if (s_time_lbl) {
        time_t now = 0;
        struct tm ti = {0};
        time(&now);
        localtime_r(&now, &ti);
        if (ti.tm_year > (2020 - 1900)) {
            snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
            lv_label_set_text(s_time_lbl, buf);
        }
    }

    // 电池
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "BAT:%d%%", soc);
        else          snprintf(buf, sizeof(buf), "BAT:--");
        lv_label_set_text(s_bat_lbl, buf);
    }

    // ── Page 0: LED 条 + 大字 + 数据行 ──
    if (s_page == 0) {
        // LED 段: 点亮 pct*20/100 段; 末端一段加白色高光 (借鉴 BambuHelper)
        int lit = st->mc_percent * LED_SEG_COUNT / 100;
        for (int i = 0; i < LED_SEG_COUNT; i++) {
            if (!s_led_seg[i]) continue;
            uint32_t col = (i < lit) ? c->success : c->border;
            if (lit > 0 && i == lit - 1) {
                // 末端高光: 绿色混白
                col = ((c->success & 0xFEFEFE) + 0x7F7F7F) >> 1;
            }
            lv_obj_set_style_bg_color(s_led_seg[i], lv_color_hex(col), 0);
        }

        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        if (s_remain_lbl) {
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0) snprintf(buf, sizeof(buf), "%d" L_HOUR "%02d" L_MIN, h, m);
                else       snprintf(buf, sizeof(buf), "%d" L_MIN, m);
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            lv_label_set_text(s_remain_lbl, buf);
        }

        // 数据行
        if (s_row_lbl[0]) {
            snprintf(buf, sizeof(buf), "%-4s %d/%d°C", L_NOZZLE,
                     (int)st->nozzle_temp, (int)st->nozzle_target);
            lv_label_set_text(s_row_lbl[0], buf);
        }
        if (s_row_dot[0])
            lv_obj_set_style_bg_color(s_row_dot[0],
                lv_color_hex(heat_led_color(st->nozzle_temp, st->nozzle_target, c)), 0);
        if (s_row_lbl[1]) {
            snprintf(buf, sizeof(buf), "%-4s %d/%d°C", L_BED,
                     (int)st->bed_temp, (int)st->bed_target);
            lv_label_set_text(s_row_lbl[1], buf);
        }
        if (s_row_dot[1])
            lv_obj_set_style_bg_color(s_row_dot[1],
                lv_color_hex(heat_led_color(st->bed_temp, st->bed_target, c)), 0);
        if (s_row_lbl[2]) {
            snprintf(buf, sizeof(buf), "%-4s %d°C", L_CHAMBER, (int)st->chamber_temp);
            lv_label_set_text(s_row_lbl[2], buf);
        }
        if (s_row_lbl[3]) {
            snprintf(buf, sizeof(buf), "%-4s %d/%d", L_LAYER,
                     st->layer_num, st->total_layer);
            lv_label_set_text(s_row_lbl[3], buf);
        }
        if (s_row_lbl[4]) {
            if (!bambu_mqtt_connected()) {
                lv_label_set_text(s_row_lbl[4], L_STATE "  " L_CONNECTING);
                if (s_row_dot[4])
                    lv_obj_set_style_bg_color(s_row_dot[4], lv_color_hex(c->error), 0);
            } else {
                snprintf(buf, sizeof(buf), "%-4s %s", L_STATE, state_text(st->state));
                lv_label_set_text(s_row_lbl[4], buf);
                if (s_row_dot[4])
                    lv_obj_set_style_bg_color(s_row_dot[4],
                        lv_color_hex(state_color(st->state, c)), 0);
            }
        }
    }

    // ── Page 1: AMS 列表 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            if (t->type[0]) {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s %d%%", i + 1, t->type, (int)t->remain);
                else       snprintf(buf, sizeof(buf), "Ext %s %d%%", t->type, (int)t->remain);
            } else {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
                else       snprintf(buf, sizeof(buf), "Ext %s", L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);

            if (s_ams_chip[i])
                lv_obj_set_style_bg_color(s_ams_chip[i],
                    lv_color_hex(tray_rgb(t->color)), 0);
            if (s_ams_bar[i])
                lv_bar_set_value(s_ams_bar[i], (int32_t)t->remain, LV_ANIM_OFF);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_industrial_page_count(void) { return s_total_pages; }
int style_industrial_current_page(void) { return s_page; }

static void show_page(int page) {
    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == page) lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        else           lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (s_pg_lbl) {
        char pg[16];
        snprintf(pg, sizeof(pg), "%d/%d", page + 1, s_total_pages);
        lv_label_set_text(s_pg_lbl, pg);
    }
}

void style_industrial_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_industrial_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
