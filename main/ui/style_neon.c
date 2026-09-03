// main/ui/style_neon.c —— 风格6: 极简霓虹极客风（哑光黑 + 浅紫/浅青，大圆角）
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

static const char *TAG __attribute__((unused)) = "style_neon";

#define CMP_NOZZLE 1
#define CMP_BED 2
#define CMP_CHAMBER 3
#define CMP_LAYER 4
#define CMP_PERCENT 5
#define CMP_REMAIN 6
#define CMP_STATE 7
#define CMP_SPEED 8

#ifndef CFG_COMPONENT_ORDER
static const int s_default_order[] = {5, 4, 1, 2, 7, 8};
#define s_order s_default_order
#define s_order_len 6
#else
static const int s_order[] = CFG_COMPONENT_ORDER;
#define s_order_len (sizeof(s_order) / sizeof(s_order[0]))
#endif

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 2;

// 两页卡片容器 (翻页时只切换可见性)
static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0 组件标签
static lv_obj_t *s_lbl[6];
static lv_obj_t *s_bar = NULL;

// Page 1 AMS 标签
static lv_obj_t *s_ams_lbl[4];

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;   // 运行时间 (HH:MM:SS)
static lv_obj_t *s_bat_lbl  = NULL;   // 电池电量

// 底部栏页码标签
static lv_obj_t *s_pg_lbl   = NULL;

// 运行时间计数器 (秒)
static uint32_t s_uptime_sec = 0;

static lv_obj_t *mk_lbl(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c) {
    if (!p) return NULL;
    lv_obj_t *l = lv_label_create(p);
    if (!l) return NULL;
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
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

// ---------------------------------------------------------------------------
// 构建 Page 0: 主状态卡片 (创建为 s_content_area 的子对象)
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    int y = 4;
    s_bar = NULL;

    for (int i = 0; i < s_order_len && i < 6; i++) {
        int cmp = s_order[i];
        char buf[48];
        const lv_font_t *font = &lv_font_montserrat_14;
        uint32_t color = c->text_primary;

        switch (cmp) {
            case CMP_PERCENT:
                font = &lv_font_montserrat_20; color = c->accent;
                snprintf(buf, sizeof(buf), "--%%"); break;
            case CMP_LAYER:    snprintf(buf, sizeof(buf), L_LAYER " --/--"); break;
            case CMP_NOZZLE:   color = c->gauge_nozzle; snprintf(buf, sizeof(buf), L_NOZZLE " --/--°C"); break;
            case CMP_BED:      color = c->gauge_bed; snprintf(buf, sizeof(buf), L_BED " --/--°C"); break;
            case CMP_CHAMBER:  color = c->gauge_chamber; snprintf(buf, sizeof(buf), L_CHAMBER " --°C"); break;
            case CMP_REMAIN:   color = c->text_secondary; snprintf(buf, sizeof(buf), L_REMAIN " --"); break;
            case CMP_STATE:    color = c->text_secondary; snprintf(buf, sizeof(buf), L_CONNECTING); break;
            case CMP_SPEED:    color = c->text_secondary; snprintf(buf, sizeof(buf), L_SPEED " -- --%%"); break;
            default: continue;
        }

        s_lbl[i] = mk_lbl(card, buf, font, color);
        if (s_lbl[i]) lv_obj_set_pos(s_lbl[i], 0, y);

        if (cmp == CMP_PERCENT) {
            y += 36;
            lv_obj_t *bar_bg = lv_obj_create(card);
            if (!bar_bg) continue;
            lv_obj_set_pos(bar_bg, 0, y);
            lv_obj_set_size(bar_bg, 200, 14);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x2A2A2A), 0);
            lv_obj_set_style_radius(bar_bg, 7, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            lv_obj_set_size(bar, 196, 10);
            lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), 0);
            lv_obj_set_style_radius(bar, 5, 0);
            lv_bar_set_range(bar, 0, 100);
            s_bar = bar;
            y += 20;
        } else {
            y += 26;
        }
    }
}

// ---------------------------------------------------------------------------
// 构建 Page 1: AMS (创建为 s_content_area 的子对象)
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, L_AMS, &lv_font_montserrat_20, c->accent);
    for (int i = 0; i < 4; i++) {
        s_ams_lbl[i] = mk_lbl(card, L_EMPTY, &lv_font_montserrat_14, c->text_secondary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 0, 36 + i * 26);
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象, 翻页时跳过)
// ---------------------------------------------------------------------------
void style_neon_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 已创建过则跳过 (翻页不再重建)
    if (s_card[0] || s_card[1]) return;

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 (持久): 左时间 + 右电池 ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    s_time_lbl = mk_lbl(header, "00:00:00", &lv_font_montserrat_14, c->accent);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_LEFT_MID, 4, 0);

    s_bat_lbl = mk_lbl(header, "BAT:--", &lv_font_montserrat_14, c->text_secondary);
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

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, &lv_font_montserrat_14, c->text_secondary);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, &lv_font_montserrat_14, c->accent);
    if (s_pg_lbl) lv_obj_align(s_pg_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    // ── 内容区域容器 ──
    s_content_area = lv_obj_create(s_scr);
    lv_obj_set_pos(s_content_area, 0, 0);
    lv_obj_set_size(s_content_area, 240, 320);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content_area, 0, 0);
    lv_obj_set_style_pad_all(s_content_area, 0, 0);
    lv_obj_remove_flag(s_content_area, LV_OBJ_FLAG_SCROLLABLE);

    // ── 创建两页卡片 (翻页只切换可见性) ──
    memset(s_lbl, 0, sizeof(s_lbl));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    build_page0();
    build_page1();

    // 隐藏非当前页
    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == s_page) lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// 更新 (每秒调用)
// ---------------------------------------------------------------------------
void style_neon_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[48];

    // 更新运行时间
    s_uptime_sec++;
    if (s_time_lbl) {
        int h = s_uptime_sec / 3600;
        int m = (s_uptime_sec % 3600) / 60;
        int s = s_uptime_sec % 60;
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
        lv_label_set_text(s_time_lbl, buf);
    }

    // 更新电池
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "BAT:%d%%", soc);
        else          snprintf(buf, sizeof(buf), "BAT:--");
        lv_label_set_text(s_bat_lbl, buf);
    }

    // 更新 page0 组件 (仅当前页可见时)
    if (s_page == 0) {
        for (int i = 0; i < s_order_len && i < 6; i++) {
            if (!s_lbl[i]) continue;
            int cmp = s_order[i];
            switch (cmp) {
                case CMP_PERCENT:
                    snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
                    lv_label_set_text(s_lbl[i], buf);
                    if (s_bar) lv_bar_set_value(s_bar, st->mc_percent, LV_ANIM_ON);
                    break;
                case CMP_LAYER:
                    snprintf(buf, sizeof(buf), L_LAYER " %d/%d", st->layer_num, st->total_layer);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_NOZZLE:
                    snprintf(buf, sizeof(buf), L_NOZZLE " %d/%d°C", (int)st->nozzle_temp, (int)st->nozzle_target);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_BED:
                    snprintf(buf, sizeof(buf), L_BED " %d/%d°C", (int)st->bed_temp, (int)st->bed_target);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_CHAMBER:
                    snprintf(buf, sizeof(buf), L_CHAMBER " %d°C", (int)st->chamber_temp);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_REMAIN:
                    if (st->mc_remaining > 0) {
                        int h = st->mc_remaining / 60, m = st->mc_remaining % 60;
                        if (h > 0)
                            snprintf(buf, sizeof(buf), L_REMAIN " %d" L_HOUR "%d" L_MIN, h, m);
                        else
                            snprintf(buf, sizeof(buf), L_REMAIN " %d" L_MIN, m);
                    } else snprintf(buf, sizeof(buf), L_REMAIN " --");
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_STATE:
                    // MQTT 未连接时显示 Connecting, 否则显示打印状态
                    if (!bambu_mqtt_connected()) {
                        lv_label_set_text(s_lbl[i], L_CONNECTING);
                        lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->error), 0);
                    } else {
                        lv_label_set_text(s_lbl[i], state_text(st->state));
                        if (st->state == BAMBU_STATE_PAUSE) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->warning), 0);
                        else if (st->state == BAMBU_STATE_FAILED) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->error), 0);
                        else if (st->state == BAMBU_STATE_RUNNING) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->success), 0);
                        else lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->text_secondary), 0);
                    }
                    break;
                case CMP_SPEED:
                    snprintf(buf, sizeof(buf), L_SPEED " %d %d%%", st->spd_lvl, st->spd_mag);
                    lv_label_set_text(s_lbl[i], buf); break;
            }
        }
    }

    // 更新 AMS page1 (仅当前页可见时)
    if (s_page == 1) {
        for (int i = 0; i < 4 && i < st->ams_count; i++) {
            if (!s_ams_lbl[i]) continue;
            bambu_ams_tray_t *t = &st->trays[i];
            if (t->type[0])
                snprintf(buf, sizeof(buf), "#%d %s %s %d%%", i+1, t->type, t->color, (int)t->remain);
            else snprintf(buf, sizeof(buf), "#%d %s", i+1, L_EMPTY);
            lv_label_set_text(s_ams_lbl[i], buf);
            lv_obj_set_style_text_color(s_ams_lbl[i],
                lv_color_hex(t->active ? c->accent : c->text_secondary), 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_neon_page_count(void) { return s_total_pages; }
int style_neon_current_page(void) { return s_page; }

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

void style_neon_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_neon_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
