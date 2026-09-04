// main/ui/style_white.c —— 风格4: 纯白素雅风（白底灰字 + 淡蓝提示）
// 布局: Page0 = 极简大字排版 (顶部状态 + 超大百分比 + 发丝线分区 + 两列小数据)
//       Page1 = AMS 极简列表 (方形色片 + 类型 + 右对齐余量)
// 设计语言: 左对齐、大留白、无边框无图标堆砌, 只靠发丝线 (1px) 分区
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
#include <time.h>

static const char *TAG __attribute__((unused)) = "style_white";

#define HAS_AMS  1
#define SLOT_COUNT 4                  // Page0 小数据位 (两行 x 两列)

#define CMP_NOZZLE  1
#define CMP_BED     2
#define CMP_CHAMBER 3
#define CMP_LAYER   4
#define CMP_PERCENT 5
#define CMP_REMAIN  6
#define CMP_STATE   7
#define CMP_SPEED   8
#define CMP_AMS     9

// 组件图标统一由 ui_theme 映射, 保证各风格语义一致
#define ICO(cmp) ui_theme_component_icon(cmp)

#ifndef CFG_COMPONENT_ORDER
static const int s_default_order[] = {5, 4, 1, 2, 6, 8};
#define s_order s_default_order
#endif
#define s_order_len (sizeof(s_order) / sizeof(s_order[0]))

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;

static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0
static lv_obj_t *s_state_lbl = NULL;          // 顶部状态行
static lv_obj_t *s_pct_lbl   = NULL;          // 超大百分比
static int       s_slot_cmp[SLOT_COUNT];      // 四个小数据位对应的组件
static lv_obj_t *s_slot_lbl[SLOT_COUNT];      // 标签 (图标 + 名称)
static lv_obj_t *s_slot_val[SLOT_COUNT];      // 数值

// Page 1
static lv_obj_t *s_ams_chip[5];               // 方形色片
static lv_obj_t *s_ams_lbl[5];                // 类型文字
static lv_obj_t *s_ams_pct[5];                // 右对齐余量

// 标题栏 / 底部栏
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_bat_lbl  = NULL;
static lv_obj_t *s_pg_lbl   = NULL;

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

// 发丝线: 素雅风的骨架 —— 不靠边框和阴影, 只用 1px 细线 + 留白分区
static lv_obj_t *mk_rule(lv_obj_t *parent, int y, int w) {
    if (!parent) return NULL;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *r = lv_obj_create(parent);
    if (!r) return NULL;
    lv_obj_set_pos(r, 0, y);
    lv_obj_set_size(r, w, 1);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(c->border), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
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

// 组件编号 -> 本地化短名
static const char *cmp_name(int cmp) {
    switch (cmp) {
        case CMP_NOZZLE:  return L_NOZZLE;
        case CMP_BED:     return L_BED;
        case CMP_CHAMBER: return L_CHAMBER;
        case CMP_LAYER:   return L_LAYER;
        case CMP_PERCENT: return "%";
        case CMP_REMAIN:  return L_REMAIN;
        case CMP_STATE:   return L_STATE;
        case CMP_SPEED:   return L_SPEED;
        case CMP_AMS:     return L_AMS;
        default:          return "";
    }
}

// 组件编号 -> 数值颜色 (温度类用各自仪表色, 其余用主文字色)
static uint32_t cmp_color(int cmp, const ui_theme_colors_t *c) {
    switch (cmp) {
        case CMP_NOZZLE:  return c->gauge_nozzle;
        case CMP_BED:     return c->gauge_bed;
        case CMP_CHAMBER: return c->gauge_chamber;
        default:          return c->text_primary;
    }
}

// 组件编号 -> 数值文本。
// 第一行数值用 L_FONT_NUM_MID (纯 Montserrat 20, 无中文 fallback), 所以剩余时间
// 统一写成 ASCII 的 "1h20m" 而不是 "1时20分": REMAIN 落在哪一行由
// CFG_COMPONENT_ORDER 决定, 一旦排到第一行中文就会变占位方块。
static void cmp_value(char *buf, size_t n, int cmp, const bambu_state_t *st) {
    switch (cmp) {
        case CMP_NOZZLE:
            snprintf(buf, n, "%d/%d°C", (int)st->nozzle_temp, (int)st->nozzle_target);
            break;
        case CMP_BED:
            snprintf(buf, n, "%d/%d°C", (int)st->bed_temp, (int)st->bed_target);
            break;
        case CMP_CHAMBER:
            snprintf(buf, n, "%d°C", (int)st->chamber_temp);
            break;
        case CMP_LAYER:
            snprintf(buf, n, "%d/%d", st->layer_num, st->total_layer);
            break;
        case CMP_PERCENT:
            snprintf(buf, n, "%d%%", st->mc_percent);
            break;
        case CMP_REMAIN:
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0) snprintf(buf, n, "%dh%02dm", h, m);
                else       snprintf(buf, n, "%dm", m);
            } else {
                snprintf(buf, n, "--");
            }
            break;
        case CMP_SPEED:
            snprintf(buf, n, "%d%%", st->spd_mag);
            break;
        case CMP_STATE:
            snprintf(buf, n, "%s", state_text(st->state));
            break;
        default:
            snprintf(buf, n, "--");
            break;
    }
}

// ---------------------------------------------------------------------------
// Page 0: 极简大字排版
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    // 阴影色取主题次要文字色而非硬编码黑, 配合 OPA_10 得到极淡的浮起感
    lv_obj_set_style_shadow_color(card, lv_color_hex(c->text_secondary), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_pad_all(card, 16, 0);          // 大留白是素雅风的核心
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // 内容区 176x170; 两列: 左 x=0, 右 x=88
    const int W = 176, col_r = 88;

    // ── 顶部状态行 (小字, 次要色) ──
    s_state_lbl = mk_lbl(card, LV_SYMBOL_WIFI " " L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_set_pos(s_state_lbl, 0, 0);

    // ── 超大百分比 (左对齐, 素雅风唯一的视觉重心) ──
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_HUGE, c->text_primary);
    if (s_pct_lbl) {
        lv_obj_set_pos(s_pct_lbl, -2, 18);          // 左移 2px 抵消字形侧边距
        lv_obj_set_style_text_color(s_pct_lbl, lv_color_hex(c->text_primary), 0);
    }

    mk_rule(card, 76, W);

    // ── 四个小数据位 (两行两列, 由 CFG_COMPONENT_ORDER 驱动) ──
    // 百分比与状态已各占固定位置, 跳过它们和 AMS, 其余按配置取前 4 项
    static const int fallback_cmp[SLOT_COUNT] = {CMP_LAYER, CMP_NOZZLE, CMP_BED, CMP_REMAIN};
    int n = 0;
    for (int i = 0; i < (int)s_order_len && n < SLOT_COUNT; i++) {
        int cmp = s_order[i];
        if (cmp == CMP_PERCENT || cmp == CMP_STATE || cmp == CMP_AMS) continue;
        s_slot_cmp[n++] = cmp;
    }
    for (int i = n; i < SLOT_COUNT; i++) s_slot_cmp[i] = fallback_cmp[i];

    memset(s_slot_lbl, 0, sizeof(s_slot_lbl));
    memset(s_slot_val, 0, sizeof(s_slot_val));

    // 第一行数值用 20px, 第二行用 14px: 形成主次层级而非平铺
    const int label_y[2] = {84, 138};
    const int value_y[2] = {100, 154};
    const lv_font_t *value_font[2] = {L_FONT_NUM_MID, L_FONT_TEXT};

    for (int i = 0; i < SLOT_COUNT; i++) {
        int cmp  = s_slot_cmp[i];
        int row  = i / 2;
        int x    = (i % 2) * col_r;

        char lbl[48];
        snprintf(lbl, sizeof(lbl), "%s %s", ICO(cmp), cmp_name(cmp));
        s_slot_lbl[i] = mk_lbl(card, lbl, L_FONT_TEXT, c->text_secondary);
        if (s_slot_lbl[i]) lv_obj_set_pos(s_slot_lbl[i], x, label_y[row]);

        s_slot_val[i] = mk_lbl(card, "--", value_font[row], cmp_color(cmp, c));
        if (s_slot_val[i]) lv_obj_set_pos(s_slot_val[i], x, value_y[row]);
    }

    mk_rule(card, 130, W);
}

// ---------------------------------------------------------------------------
// Page 1: AMS 极简列表
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 218);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(c->text_secondary), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_10, 0);
    lv_obj_set_style_shadow_width(card, 8, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    const int W = 176;

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->text_primary);
    mk_rule(card, 30, W);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_pct, 0, sizeof(s_ams_pct));

    for (int i = 0; i < 5; i++) {
        int y = 42 + i * 26;

        // 方形色片 (极简: 小方块而非圆形/菱形), 颜色由 MQTT tray_color 实时驱动
        lv_obj_t *chip = lv_obj_create(card);
        if (chip) {
            lv_obj_set_pos(chip, 0, y + 1);
            lv_obj_set_size(chip, 12, 12);
            lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(chip, 1, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->border), 0);
            lv_obj_set_style_pad_all(chip, 0, 0);
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(chip, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_chip[i] = chip;

        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 20, y);

        // 余量右对齐: align 写的是样式, 文字变长后布局引擎会重新右对齐
        s_ams_pct[i] = mk_lbl(card, "--", L_FONT_NUM, c->text_secondary);
        if (s_ams_pct[i]) lv_obj_align(s_ams_pct[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_white_build(void) {
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

    // 标题栏/底部栏文字颜色按各自背景亮度自动取黑/白, 不硬编码
    const uint32_t htxt = ui_theme_on_color(c->header_bg);
    const uint32_t ftxt = ui_theme_on_color(c->footer_bg);

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, htxt);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, htxt);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, htxt);
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

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, ftxt);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, L_FONT_NUM, ftxt);
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
void style_white_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[64];

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

    // 电池 (图标随电量四档变化)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
    }

    // ── Page 0 ──
    if (s_page == 0) {
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }

        if (s_state_lbl) {
            bool conn = bambu_mqtt_connected();
            uint32_t sc = conn ? state_color(st->state, c) : c->error;
            if (conn) snprintf(buf, sizeof(buf), "%s %s",
                               ui_theme_state_icon(st->state, true), state_text(st->state));
            else      snprintf(buf, sizeof(buf), "%s " L_CONNECTING, LV_SYMBOL_WIFI);
            lv_label_set_text(s_state_lbl, buf);
            lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(sc), 0);
        }

        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!s_slot_val[i]) continue;
            cmp_value(buf, sizeof(buf), s_slot_cmp[i], st);
            lv_label_set_text(s_slot_val[i], buf);
        }
    }

    // ── Page 1: AMS 列表 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            if (t->type[0]) {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, t->type);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, t->type);
            } else {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);
            // 当前料槽高亮为强调色
            lv_obj_set_style_text_color(s_ams_lbl[i],
                lv_color_hex(t->active ? c->accent : c->text_primary), 0);

            if (s_ams_pct[i]) {
                snprintf(buf, sizeof(buf), "%d%%", (int)t->remain);
                lv_label_set_text(s_ams_pct[i], buf);
            }
            if (s_ams_chip[i]) ui_theme_tray_swatch(s_ams_chip[i], t);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_white_page_count(void) { return s_total_pages; }
int style_white_current_page(void) { return s_page; }

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

void style_white_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_white_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
