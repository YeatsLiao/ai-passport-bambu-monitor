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
#include <stdlib.h>
#include <time.h>

static const char *TAG __attribute__((unused)) = "style_neon";

#define CMP_NOZZLE 1
#define CMP_BED 2
#define CMP_CHAMBER 3
#define CMP_LAYER 4
#define CMP_PERCENT 5
#define CMP_REMAIN 6
#define CMP_STATE 7
#define CMP_SPEED 8

// 组件图标统一由 ui_theme 映射, 保证 6 个风格语义一致
#define ICO(cmp) ui_theme_component_icon(cmp)

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

// Page 1 AMS 标签 (索引 0-3: AMS 料槽, 4: 外挂料槽 Ext)
static lv_obj_t *s_ams_lbl[5];
static lv_obj_t *s_ams_swatch[5];   // 颜色色块 (颜色来自 MQTT tray_color)

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;   // 实时时间 (NTP 同步, HH:MM)
static lv_obj_t *s_bat_lbl  = NULL;   // 电池电量

// 底部栏页码标签
static lv_obj_t *s_pg_lbl   = NULL;

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
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 与其它风格一致
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
        // 含本地化文字的行用 L_FONT_TEXT (中文模式回落到中文字体),
        // 纯数字行用 L_FONT_NUM* (两种语言都是 Montserrat)
        const lv_font_t *font = L_FONT_TEXT;
        uint32_t color = c->text_primary;

        switch (cmp) {
            case CMP_PERCENT:
                font = L_FONT_NUM_MID; color = c->accent;
                snprintf(buf, sizeof(buf), "--%%"); break;
            case CMP_LAYER:
                snprintf(buf, sizeof(buf), "%s " L_LAYER " --/--", ICO(CMP_LAYER)); break;
            case CMP_NOZZLE:
                color = c->gauge_nozzle;
                snprintf(buf, sizeof(buf), "%s " L_NOZZLE " --/--°C", ICO(CMP_NOZZLE)); break;
            case CMP_BED:
                color = c->gauge_bed;
                snprintf(buf, sizeof(buf), "%s " L_BED " --/--°C", ICO(CMP_BED)); break;
            case CMP_CHAMBER:
                color = c->gauge_chamber;
                snprintf(buf, sizeof(buf), "%s " L_CHAMBER " --°C", ICO(CMP_CHAMBER)); break;
            case CMP_REMAIN:
                color = c->text_secondary;
                snprintf(buf, sizeof(buf), "%s " L_REMAIN " --", ICO(CMP_REMAIN)); break;
            case CMP_STATE:
                color = c->text_secondary;
                snprintf(buf, sizeof(buf), "%s " L_CONNECTING, LV_SYMBOL_WIFI); break;
            case CMP_SPEED:
                color = c->text_secondary;
                snprintf(buf, sizeof(buf), "%s " L_SPEED " -- --%%", ICO(CMP_SPEED)); break;
            default: continue;
        }

        s_lbl[i] = mk_lbl(card, buf, font, color);
        if (s_lbl[i]) lv_obj_set_pos(s_lbl[i], 0, y);

        if (cmp == CMP_PERCENT) {
            y += 42;   // 卡片加高后行距拉开
            lv_obj_t *bar_bg = lv_obj_create(card);
            if (!bar_bg) continue;
            lv_obj_set_pos(bar_bg, 0, y);
            lv_obj_set_size(bar_bg, 200, 14);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->bg), 0);
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
            y += 22;
        } else {
            y += 32;
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
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 与其它风格一致
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->accent);
    for (int i = 0; i < 5; i++) {
        // 色块 (颜色在 update 中由 MQTT 推送的 tray_color 填充)
        lv_obj_t *sw = lv_obj_create(card);
        if (sw) {
            lv_obj_set_size(sw, 14, 14);
            lv_obj_set_pos(sw, 0, 40 + i * 36 + 3);
            lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(sw, 3, 0);
            lv_obj_set_style_border_width(sw, 1, 0);
            lv_obj_set_style_border_color(sw, lv_color_hex(c->border), 0);
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(sw, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_swatch[i] = sw;

        s_ams_lbl[i] = mk_lbl(card, L_EMPTY, L_FONT_TEXT, c->text_secondary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 20, 40 + i * 36);
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

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, c->accent);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, c->text_primary);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (图标字形只在 Montserrat 内, 本行无中文, 用 SYMBOL 字体)
    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, c->text_secondary);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (持久) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    // 深底风格 footer 与背景同色, 加 1px 顶边线让底栏有边界, 不显空
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, c->text_secondary);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, L_FONT_NUM, c->accent);
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
    memset(s_ams_swatch, 0, sizeof(s_ams_swatch));
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

    // 更新时间（NTP 同步成功后显示实时时间，未同步显示 --:--）
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

    // 更新电池 (图标随电量分档)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        // 图标颜色随电量分档 (满电绿 / 中低黄 / 低电红), 由实时数据驱动
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
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
                    snprintf(buf, sizeof(buf), "%s " L_LAYER " %d/%d",
                             ICO(CMP_LAYER), st->layer_num, st->total_layer);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_NOZZLE:
                    snprintf(buf, sizeof(buf), "%s " L_NOZZLE " %d/%d°C",
                             ICO(CMP_NOZZLE), (int)st->nozzle_temp, (int)st->nozzle_target);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_BED:
                    snprintf(buf, sizeof(buf), "%s " L_BED " %d/%d°C",
                             ICO(CMP_BED), (int)st->bed_temp, (int)st->bed_target);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_CHAMBER:
                    snprintf(buf, sizeof(buf), "%s " L_CHAMBER " %d°C",
                             ICO(CMP_CHAMBER), (int)st->chamber_temp);
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_REMAIN:
                    if (st->mc_remaining > 0) {
                        int h = st->mc_remaining / 60, m = st->mc_remaining % 60;
                        if (h > 0)
                            snprintf(buf, sizeof(buf), "%s " L_REMAIN " %d" L_HOUR "%d" L_MIN,
                                     ICO(CMP_REMAIN), h, m);
                        else
                            snprintf(buf, sizeof(buf), "%s " L_REMAIN " %d" L_MIN,
                                     ICO(CMP_REMAIN), m);
                    } else snprintf(buf, sizeof(buf), "%s " L_REMAIN " --", ICO(CMP_REMAIN));
                    lv_label_set_text(s_lbl[i], buf); break;
                case CMP_STATE: {
                    // MQTT 未连接时显示 Connecting, 否则显示打印状态 (图标随状态变化)
                    bool conn = bambu_mqtt_connected();
                    if (conn) {
                        snprintf(buf, sizeof(buf), "%s %s",
                                 ui_theme_state_icon(st->state, true), state_text(st->state));
                        lv_label_set_text(s_lbl[i], buf);
                    } else {
                        snprintf(buf, sizeof(buf), "%s " L_CONNECTING, LV_SYMBOL_WIFI);
                        lv_label_set_text(s_lbl[i], buf);
                    }
                    if (!conn) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->error), 0);
                    else if (st->state == BAMBU_STATE_PAUSE) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->warning), 0);
                    else if (st->state == BAMBU_STATE_FAILED) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->error), 0);
                    else if (st->state == BAMBU_STATE_RUNNING) lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->success), 0);
                    else lv_obj_set_style_text_color(s_lbl[i], lv_color_hex(c->text_secondary), 0);
                    break;
                }
                case CMP_SPEED:
                    snprintf(buf, sizeof(buf), "%s " L_SPEED " %d %d%%",
                             ICO(CMP_SPEED), st->spd_lvl, st->spd_mag);
                    lv_label_set_text(s_lbl[i], buf); break;
            }
        }
    }

    // 更新 AMS page1 (仅当前页可见时); 第 5 行为外挂料槽 Ext
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;   // 未上报的料槽保持原样
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            char name[8];
            if (i < 4) snprintf(name, sizeof(name), "#%d", i + 1);
            else       snprintf(name, sizeof(name), "%s", L_EXT);
            if (t->type[0])
                snprintf(buf, sizeof(buf), "%s %s %d%%", name, t->type, (int)t->remain);
            else
                snprintf(buf, sizeof(buf), "%s %s", name, L_EMPTY);
            lv_label_set_text(s_ams_lbl[i], buf);
            lv_obj_set_style_text_color(s_ams_lbl[i],
                lv_color_hex(t->active ? c->accent : c->text_secondary), 0);

            // 色块按 MQTT 实时数据渲染 (透明料画空心描边)
            if (s_ams_swatch[i])
                ui_theme_tray_swatch(s_ams_swatch[i], t);
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
