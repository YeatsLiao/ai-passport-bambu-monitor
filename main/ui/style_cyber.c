// main/ui/style_cyber.c —— 风格2: 赛博极简监控风（纯黑底 + 冰蓝霓虹）
#include "ui_monitor.h"
#include "ui_theme.h"
#include "ui_lang.h"
#include "../bambu_state.h"
#include "../bambu_mqtt.h"
#include "../config.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG __attribute__((unused)) = "style_cyber";

#define CMP_NOZZLE 1
#define CMP_BED 2
#define CMP_CHAMBER 3
#define CMP_LAYER 4
#define CMP_PERCENT 5
#define CMP_REMAIN 6
#define CMP_STATE 7
#define CMP_SPEED 8
#define CMP_AMS 9

#ifndef CFG_COMPONENT_ORDER
static const int s_default_order[] = {5, 4, 1, 2, 7, 8};
#define s_order s_default_order
#define s_order_len 6
#else
static const int s_order[] = CFG_COMPONENT_ORDER;
#define s_order_len (sizeof(s_order) / sizeof(s_order[0]))
#endif

#define HAS_AMS 1

static lv_obj_t *s_page_indicator = NULL;
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;
static lv_obj_t *s_lbl[6];
static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_ams_lbl[4];

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

static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_scr);
    lv_obj_set_pos(card, 6, 34);
    lv_obj_set_size(card, 228, 216);
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
                font = &lv_font_montserrat_20;
                color = c->accent;
                snprintf(buf, sizeof(buf), "--%%");
                break;
            case CMP_LAYER:    snprintf(buf, sizeof(buf), L_LAYER " --/--"); break;
            case CMP_NOZZLE:   color = c->gauge_nozzle; snprintf(buf, sizeof(buf), L_NOZZLE " --/--°C"); break;
            case CMP_BED:      color = c->gauge_bed; snprintf(buf, sizeof(buf), L_BED " --/--°C"); break;
            case CMP_CHAMBER:  color = c->gauge_chamber; snprintf(buf, sizeof(buf), L_CHAMBER " --°C"); break;
            case CMP_REMAIN:   color = c->text_secondary; snprintf(buf, sizeof(buf), L_REMAIN " --"); break;
            case CMP_STATE:    color = c->text_secondary; snprintf(buf, sizeof(buf), L_STATE_IDLE); break;
            case CMP_SPEED:    color = c->text_secondary; snprintf(buf, sizeof(buf), L_SPEED " -- --%%"); break;
            default: continue;
        }

        s_lbl[i] = mk_lbl(card, buf, font, color);
        lv_obj_set_pos(s_lbl[i], 0, y);

        if (cmp == CMP_PERCENT) {
            y += 36;
            lv_obj_t *bar_bg = lv_obj_create(card);
            lv_obj_set_pos(bar_bg, 0, y);
            lv_obj_set_size(bar_bg, 204, 10);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(0x222222), 0);
            lv_obj_set_style_radius(bar_bg, 5, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            lv_obj_set_size(bar, 200, 6);
            lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), 0);
            lv_obj_set_style_radius(bar, 3, 0);
            lv_bar_set_range(bar, 0, 100);
            s_bar = bar;
            y += 16;
        } else {
            y += 26;
        }
    }
}

static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_scr);
    lv_obj_set_pos(card, 6, 34);
    lv_obj_set_size(card, 228, 216);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, L_AMS, &lv_font_montserrat_20, c->accent);
    for (int i = 0; i < 4; i++) {
        s_ams_lbl[i] = mk_lbl(card, L_EMPTY, &lv_font_montserrat_14, c->text_secondary);
        lv_obj_set_pos(s_ams_lbl[i], 0, 36 + i * 26);
    }
}

void style_cyber_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 清空旧指针
    memset(s_lbl, 0, sizeof(s_lbl));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    s_bar = NULL;
    s_page_indicator = NULL;
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // 标题栏
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_border_color(header, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);

    mk_lbl(header, L_TITLE_BAMBU, &lv_font_montserrat_14, c->accent);

    lv_obj_t *pct_lbl = mk_lbl(header, "--%%", &lv_font_montserrat_14, c->accent);
    lv_obj_align(pct_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *status = mk_lbl(header, L_CONNECTING, &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -50, 0);

    // 底部栏
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);

    mk_lbl(footer, L_NAV_HINT, &lv_font_montserrat_14, c->text_secondary);
    s_page_indicator = mk_lbl(footer, "1/2", &lv_font_montserrat_14, c->accent);
    lv_obj_align(s_page_indicator, LV_ALIGN_RIGHT_MID, -8, 0);

    if (s_page == 1) build_page1();
    else             build_page0();
}

void style_cyber_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[48];

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
                    snprintf(buf, sizeof(buf), L_REMAIN " %d" L_HOUR "%d" L_MIN, h, m);
                } else snprintf(buf, sizeof(buf), L_REMAIN " --");
                lv_label_set_text(s_lbl[i], buf); break;
            case CMP_STATE:
                lv_label_set_text(s_lbl[i], state_text(st->state));
                lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(state_color(st->state, c)), 0);
                break;
            case CMP_SPEED:
                snprintf(buf, sizeof(buf), L_SPEED " %d %d%%", st->spd_lvl, st->spd_mag);
                lv_label_set_text(s_lbl[i], buf); break;
        }
    }

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

int style_cyber_page_count(void) { return s_total_pages; }
int style_cyber_current_page(void) { return s_page; }

void style_cyber_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    rebuild_page();
}
void style_cyber_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    rebuild_page();
}
