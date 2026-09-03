// main/ui/style_bambu.c —— 风格1: 拓竹原厂工业风
// 布局: Page0 = 圆环进度仪表 (lv_arc) + 状态行 + 2x2 数据网格
//       Page1 = 拓竹同款 AMS 料槽格子 (背景=耗材真实颜色, 亮度对比度自动黑白字)
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

static const char *TAG __attribute__((unused)) = "style_bambu";

#define HAS_AMS  1

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;

// 两页卡片容器 (翻页时只切换可见性)
static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0: 圆环仪表 + 状态行 + 数据网格
static lv_obj_t *s_arc        = NULL;   // 进度圆环
static lv_obj_t *s_pct_lbl    = NULL;   // 圆环中央大字百分比
static lv_obj_t *s_state_dot  = NULL;   // 状态彩点
static lv_obj_t *s_state_lbl  = NULL;   // 状态文字
static lv_obj_t *s_nozzle_lbl = NULL;   // 喷嘴温度
static lv_obj_t *s_bed_lbl    = NULL;   // 热床温度
static lv_obj_t *s_layer_lbl  = NULL;   // 层数
static lv_obj_t *s_remain_lbl = NULL;   // 剩余时间

// Page 1: AMS 料槽格子 (0-3: AMS 槽, 4: Ext 外挂)
static lv_obj_t *s_ams_box[5];          // 色块格子 (背景=耗材颜色)
static lv_obj_t *s_ams_type[5];         // 格内耗材类型文字
static lv_obj_t *s_ams_rem[5];          // 格内余量文字

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;     // 实时时间 (NTP 同步, HH:MM)
static lv_obj_t *s_bat_lbl  = NULL;     // 电池电量

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

// tray_color "RRGGBBAA" -> uint32_t RGB (无效返回灰色)
static uint32_t tray_rgb(const char *hex) {
    uint32_t rgb = 0x888888;
    if (hex && strlen(hex) >= 6) {
        char buf[7] = {0};
        memcpy(buf, hex, 6);
        rgb = (uint32_t)strtoul(buf, NULL, 16);
    }
    return rgb;
}

// 创建一个小圆点 (状态指示)
static lv_obj_t *mk_dot(lv_obj_t *parent, int x, int y, uint32_t color) {
    lv_obj_t *d = lv_obj_create(parent);
    if (!d) return NULL;
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, 10, 10);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_bg_color(d, lv_color_hex(color), 0);
    return d;
}

// ---------------------------------------------------------------------------
// Page 0: 圆环进度仪表 + 状态行 + 2x2 数据网格
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius + 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 圆环进度仪表 (270° 弧, 顶部居中) ──
    lv_obj_t *arc = lv_arc_create(card);
    if (arc) {
        lv_obj_set_pos(arc, 40, 0);
        lv_obj_set_size(arc, 128, 128);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);          // 隐藏旋钮
        lv_obj_set_style_arc_width(arc, 14, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 14, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(c->border), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(c->accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    }
    s_arc = arc;

    // 中央大字百分比
    s_pct_lbl = mk_lbl(card, "--%", &lv_font_montserrat_28, c->text_primary);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, 50);

    // ── 状态行: 彩点 + 状态文字 ──
    s_state_dot = mk_dot(card, 58, 138, c->text_secondary);
    s_state_lbl = mk_lbl(card, L_CONNECTING, &lv_font_montserrat_14, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_MID, 8, 136);

    // ── 2x2 数据网格 (温度/层数/剩余时间) ──
    const lv_font_t *val_font = &lv_font_montserrat_14;
    s_nozzle_lbl = mk_lbl(card, L_NOZZLE " --/--°C", val_font, c->gauge_nozzle);
    s_bed_lbl    = mk_lbl(card, L_BED " --/--°C",    val_font, c->gauge_bed);
    s_layer_lbl  = mk_lbl(card, L_LAYER " --/--",    val_font, c->text_primary);
    s_remain_lbl = mk_lbl(card, L_REMAIN " --",      val_font, c->text_primary);
    if (s_nozzle_lbl) lv_obj_set_pos(s_nozzle_lbl, 6,  166);
    if (s_bed_lbl)    lv_obj_set_pos(s_bed_lbl,    116, 166);
    if (s_layer_lbl)  lv_obj_set_pos(s_layer_lbl,   6,  192);
    if (s_remain_lbl) lv_obj_set_pos(s_remain_lbl, 116, 192);
}

// ---------------------------------------------------------------------------
// Page 1: 拓竹同款 AMS 料槽格子 (背景 = 耗材真实颜色)
// ---------------------------------------------------------------------------
static lv_obj_t *mk_slot(lv_obj_t *parent, int x, int y, int w, int h) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *box = lv_obj_create(parent);
    if (!box) return NULL;
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(box, 8, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(box, 2, 0);
    return box;
}

static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius + 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, L_AMS, &lv_font_montserrat_20, c->accent);

    // ── 4 个 AMS 料槽格子 (2x2) ──
    const int sx[4] = {0, 104, 0, 104};
    const int sy[4] = {30, 30, 98, 98};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *box = mk_slot(card, sx[i], sy[i], 100, 62);
        s_ams_box[i] = box;
        s_ams_type[i] = NULL;
        s_ams_rem[i] = NULL;
        if (!box) continue;
        // 槽号 (左上角小字, 格子内相对定位)
        lv_obj_t *num = mk_lbl(box, "", &lv_font_montserrat_14, 0xFFFFFF);
        if (num) {
            lv_obj_align(num, LV_ALIGN_TOP_LEFT, 4, 2);
            char n[8];
            snprintf(n, sizeof(n), "#%d", i + 1);
            lv_label_set_text(num, n);
        }
        // 类型 (居中大字) + 余量 (下方小字)
        s_ams_type[i] = mk_lbl(box, L_EMPTY, &lv_font_montserrat_14, 0xFFFFFF);
        if (s_ams_type[i]) lv_obj_align(s_ams_type[i], LV_ALIGN_CENTER, 0, -6);
        s_ams_rem[i] = mk_lbl(box, "--", &lv_font_montserrat_14, 0xFFFFFF);
        if (s_ams_rem[i]) lv_obj_align(s_ams_rem[i], LV_ALIGN_CENTER, 0, 12);
    }

    // ── Ext 外挂料槽 (底部宽格子) ──
    lv_obj_t *box = mk_slot(card, 0, 166, 204, 44);
    s_ams_box[4] = box;
    s_ams_type[4] = NULL;
    s_ams_rem[4] = NULL;
    if (box) {
        lv_obj_t *num = mk_lbl(box, "Ext", &lv_font_montserrat_14, 0xFFFFFF);
        if (num) lv_obj_align(num, LV_ALIGN_LEFT_MID, 6, 0);
        s_ams_type[4] = mk_lbl(box, L_EMPTY, &lv_font_montserrat_14, 0xFFFFFF);
        if (s_ams_type[4]) lv_obj_align(s_ams_type[4], LV_ALIGN_LEFT_MID, 40, 0);
        s_ams_rem[4] = mk_lbl(box, "--", &lv_font_montserrat_14, 0xFFFFFF);
        if (s_ams_rem[4]) lv_obj_align(s_ams_rem[4], LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_bambu_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 已创建过则跳过 (翻页不再重建)
    if (s_card[0] || s_card[1]) return;

    // 屏幕背景
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

    // ── 创建两页卡片 (都创建, 翻页只切换可见性) ──
    memset(s_card, 0, sizeof(s_card));
    memset(s_ams_box, 0, sizeof(s_ams_box));
    memset(s_ams_type, 0, sizeof(s_ams_type));
    memset(s_ams_rem, 0, sizeof(s_ams_rem));
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
void style_bambu_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[48];

    // 时间 (NTP 同步成功后显示实时时间)
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

    // ── Page 0: 仪表 + 网格 ──
    if (s_page == 0) {
        if (s_arc) lv_arc_set_value(s_arc, st->mc_percent);
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        if (s_state_lbl) {
            if (!bambu_mqtt_connected()) {
                lv_label_set_text(s_state_lbl, L_CONNECTING);
                lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(c->error), 0);
                if (s_state_dot)
                    lv_obj_set_style_bg_color(s_state_dot, lv_color_hex(c->error), 0);
            } else {
                lv_label_set_text(s_state_lbl, state_text(st->state));
                uint32_t sc = state_color(st->state, c);
                lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(sc), 0);
                if (s_state_dot)
                    lv_obj_set_style_bg_color(s_state_dot, lv_color_hex(sc), 0);
            }
        }
        if (s_nozzle_lbl) {
            snprintf(buf, sizeof(buf), L_NOZZLE " %d/%d°C",
                     (int)st->nozzle_temp, (int)st->nozzle_target);
            lv_label_set_text(s_nozzle_lbl, buf);
        }
        if (s_bed_lbl) {
            snprintf(buf, sizeof(buf), L_BED " %d/%d°C",
                     (int)st->bed_temp, (int)st->bed_target);
            lv_label_set_text(s_bed_lbl, buf);
        }
        if (s_layer_lbl) {
            snprintf(buf, sizeof(buf), L_LAYER " %d/%d",
                     st->layer_num, st->total_layer);
            lv_label_set_text(s_layer_lbl, buf);
        }
        if (s_remain_lbl) {
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0)
                    snprintf(buf, sizeof(buf), L_REMAIN " %d" L_HOUR "%d" L_MIN, h, m);
                else
                    snprintf(buf, sizeof(buf), L_REMAIN " %d" L_MIN, m);
            } else {
                snprintf(buf, sizeof(buf), L_REMAIN " --");
            }
            lv_label_set_text(s_remain_lbl, buf);
        }
    }

    // ── Page 1: AMS 料槽格子上色 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_box[i] || !s_ams_type[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;   // 未上报的料槽保持空槽
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            uint32_t rgb = tray_rgb(t->color);

            if (!t->type[0]) {
                // 空槽: 边框灰底 + 灰字
                lv_obj_set_style_bg_color(s_ams_box[i], lv_color_hex(c->border), 0);
                lv_obj_set_style_text_color(s_ams_type[i],
                    lv_color_hex(c->text_secondary), 0);
                lv_label_set_text(s_ams_type[i], L_EMPTY);
                if (s_ams_rem[i]) lv_label_set_text(s_ams_rem[i], "");
                continue;
            }

            // 有料: 背景 = 耗材颜色, 文字按亮度自动黑白
            lv_obj_set_style_bg_color(s_ams_box[i], lv_color_hex(rgb), 0);
            lv_color_t tc = ui_theme_contrast_text(rgb);
            lv_obj_set_style_text_color(s_ams_type[i], tc, 0);
            lv_obj_set_style_text_color(s_ams_rem[i], tc, 0);
            lv_label_set_text(s_ams_type[i], t->type);
            if (s_ams_rem[i]) {
                snprintf(buf, sizeof(buf), "%d%%", (int)t->remain);
                lv_label_set_text(s_ams_rem[i], buf);
            }
        }

        // 当前使用中的料槽: 白色高亮边框
        for (int i = 0; i < 5; i++) {
            if (!s_ams_box[i]) continue;
            bool active = (i < 4) ? st->trays[i].active : (st->active_tray >= 4);
            lv_obj_set_style_border_color(s_ams_box[i],
                lv_color_hex(active ? 0xFFFFFF : c->card_bg), 0);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_bambu_page_count(void) { return s_total_pages; }
int style_bambu_current_page(void) { return s_page; }

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

void style_bambu_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_bambu_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
