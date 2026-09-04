// main/ui/style_cyber.c —— 风格2: 赛博极简监控风
// 布局: Page0 = 四角括号框 (HUD 风格) + 发光大字百分比 + 左侧霓虹竖条数据行
//       Page1 = AMS 霓虹色片列表 (发光边框高亮当前料槽)
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

static const char *TAG __attribute__((unused)) = "style_cyber";

#define HAS_AMS  1

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

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;

static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0: HUD 框角 + 发光百分比 + 霓虹数据行
static lv_obj_t *s_pct_lbl     = NULL;   // 发光大字百分比
static lv_obj_t *s_state_lbl   = NULL;   // 状态文字 (框角内顶部)
static lv_obj_t *s_row_bar[5];           // 数据行左侧霓虹竖条
static lv_obj_t *s_row_lbl[5];           // 数据行文字 (NOZ/BED/CHM/LAYER/REMAIN)
static lv_obj_t *s_layer_lbl   = NULL;   // 层数 (框角内底部, 覆盖 row[3] 的复用)
static lv_obj_t *s_remain_lbl  = NULL;   // 剩余时间

// Page 1: AMS 列表
static lv_obj_t *s_ams_chip[5];          // 霓虹色片 (发光边框)
static lv_obj_t *s_ams_lbl[5];           // 文字

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_bat_lbl  = NULL;

// 底部栏页码标签
static lv_obj_t *s_pg_lbl = NULL;

// 霓虹数据行对应的组件 (build/update 共用, 保证图标与内容一致)
static const int s_row_cmp[5] = {CMP_NOZZLE, CMP_BED, CMP_CHAMBER, CMP_SPEED, CMP_REMAIN};

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

// HUD 四角括号: 4 个 L 形角标 (用带单边边框的小方块拼出)
static void mk_hud_corners(lv_obj_t *parent, int x, int y, int w, int h) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    const int len = 14;                       // 角标臂长
    const int th = 2;                         // 线宽
    const int pos[4][2] = {{x, y}, {x + w - len, y}, {x, y + h - len}, {x + w - len, y + h - len}};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *corner = lv_obj_create(parent);
        if (!corner) continue;
        lv_obj_set_pos(corner, pos[i][0], pos[i][1]);
        lv_obj_set_size(corner, len, len);
        lv_obj_remove_flag(corner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(corner, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(corner, 0, 0);
        lv_obj_set_style_border_color(corner, lv_color_hex(c->accent), 0);
        lv_obj_set_style_border_width(corner, 0, 0);
        // 按象限显示两条边
        bool left  = (i == 0 || i == 2);
        bool top   = (i == 0 || i == 1);
        lv_obj_set_style_border_width(corner, th, LV_PART_MAIN);
        if (left && top) { lv_obj_set_style_border_side(corner, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_LEFT, 0); }
        else if (!left && top) { lv_obj_set_style_border_side(corner, LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_RIGHT, 0); }
        else if (left && !top) { lv_obj_set_style_border_side(corner, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT, 0); }
        else { lv_obj_set_style_border_side(corner, LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_RIGHT, 0); }
    }
}

// 霓虹竖条 (数据行左侧)
static lv_obj_t *mk_neon_bar(lv_obj_t *parent, int x, int y) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *b = lv_obj_create(parent);
    if (!b) return NULL;
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 3, 16);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b, 1, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(c->accent), 0);
    lv_obj_set_style_shadow_color(b, lv_color_hex(c->accent), 0);
    lv_obj_set_style_shadow_width(b, 6, 0);
    lv_obj_set_style_shadow_opa(b, LV_OPA_40, 0);
    return b;
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

// ---------------------------------------------------------------------------
// Page 0: HUD 框角 + 发光百分比 + 霓虹数据行
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 与其它风格一致
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 0, 0);                 // 赛博风: 直角
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── HUD 四角括号框 (含状态行 + 大字百分比 + 层数/剩余) ──
    mk_hud_corners(card, 0, 0, 208, 100);

    // 状态行 (框内顶部居中, 图标随状态变化)
    s_state_lbl = mk_lbl(card, LV_SYMBOL_WIFI " " L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_MID, 0, 8);

    // 发光大字百分比 (带霓虹光晕)
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_HUGE, c->accent);
    if (s_pct_lbl) {
        lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, 26);
        lv_obj_set_style_shadow_color(s_pct_lbl, lv_color_hex(c->accent), 0);
        lv_obj_set_style_shadow_width(s_pct_lbl, 12, 0);
        lv_obj_set_style_shadow_opa(s_pct_lbl, LV_OPA_50, 0);
    }

    // 层数 / 剩余时间 (框内底部一行, 居中)
    // HUD 框内宽度有限, 这两行用"图标 + 纯数字"代替文字标签, 中英文宽度一致
    s_layer_lbl = mk_lbl(card, "--/--", L_FONT_SYMBOL, c->text_secondary);
    if (s_layer_lbl) lv_obj_align(s_layer_lbl, LV_ALIGN_TOP_MID, -46, 78);
    s_remain_lbl = mk_lbl(card, "--", L_FONT_SYMBOL, c->text_secondary);
    if (s_remain_lbl) lv_obj_align(s_remain_lbl, LV_ALIGN_TOP_MID, 50, 78);

    // ── 霓虹竖条数据行: NOZ / BED / CHM / SPEED / REMAIN ──
    memset(s_row_bar, 0, sizeof(s_row_bar));
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    const char *inits[5] = {
        L_NOZZLE " --/--°C", L_BED " --/--°C", L_CHAMBER " --°C",
        L_SPEED " -- --%", L_REMAIN " --"
    };
    for (int i = 0; i < 5; i++) {
        s_row_bar[i] = mk_neon_bar(card, 2, 122 + i * 24);   // 卡片加高后行距拉开
        char init[48];
        snprintf(init, sizeof(init), "%s %s", ICO(s_row_cmp[i]), inits[i]);
        s_row_lbl[i] = mk_lbl(card, init, L_FONT_TEXT,
                              (i < 3) ? c->text_primary : c->text_secondary);
        if (s_row_lbl[i]) lv_obj_set_pos(s_row_lbl[i], 14, 118 + i * 24);
    }
}

// ---------------------------------------------------------------------------
// Page 1: AMS 霓虹色片列表
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 与其它风格一致
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->accent);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));

    for (int i = 0; i < 5; i++) {
        int y = 40 + i * 40;   // 卡片加高后行距拉开
        // 霓虹色片 (带光晕, 颜色来自 MQTT tray_color)
        lv_obj_t *chip = lv_obj_create(card);
        if (chip) {
            lv_obj_set_pos(chip, 0, y);
            lv_obj_set_size(chip, 18, 18);
            lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(chip, 3, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->accent), 0);
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(chip, lv_color_hex(c->text_secondary), 0);
            lv_obj_set_style_shadow_width(chip, 8, 0);
            lv_obj_set_style_shadow_opa(chip, LV_OPA_30, 0);
        }
        s_ams_chip[i] = chip;

        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 26, y + 1);
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_cyber_build(void) {
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

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, c->accent);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, c->accent);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (本行无中文, 用 SYMBOL 字体)
    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, c->accent);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (持久) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    // 深底风格 footer 与背景同色, 加 1px 冰蓝顶边线与卡片呼应, 底部不显空
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, c->accent);
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
void style_cyber_update(void) {
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

    // 电池 (图标随电量分档)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        // 图标颜色随电量分档 (满电绿 / 中低黄 / 低电红), 由实时数据驱动
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    // ── Page 0: HUD + 霓虹行 ──
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
        // HUD 框内两行: 图标 + 纯数字 (中英文宽度一致)
        if (s_layer_lbl) {
            snprintf(buf, sizeof(buf), "%s %d/%d", ICO(CMP_LAYER),
                     st->layer_num, st->total_layer);
            lv_label_set_text(s_layer_lbl, buf);
        }
        if (s_remain_lbl) {
            if (st->mc_remaining > 0)
                snprintf(buf, sizeof(buf), "%s %dh%02dm", ICO(CMP_REMAIN),
                         st->mc_remaining / 60, st->mc_remaining % 60);
            else
                snprintf(buf, sizeof(buf), "%s --", ICO(CMP_REMAIN));
            lv_label_set_text(s_remain_lbl, buf);
        }
        // 霓虹竖条数据行
        if (s_row_lbl[0]) {
            snprintf(buf, sizeof(buf), "%s " L_NOZZLE " %d/%d°C", ICO(CMP_NOZZLE),
                     (int)st->nozzle_temp, (int)st->nozzle_target);
            lv_label_set_text(s_row_lbl[0], buf);
        }
        if (s_row_lbl[1]) {
            snprintf(buf, sizeof(buf), "%s " L_BED " %d/%d°C", ICO(CMP_BED),
                     (int)st->bed_temp, (int)st->bed_target);
            lv_label_set_text(s_row_lbl[1], buf);
        }
        if (s_row_lbl[2]) {
            snprintf(buf, sizeof(buf), "%s " L_CHAMBER " %d°C", ICO(CMP_CHAMBER),
                     (int)st->chamber_temp);
            lv_label_set_text(s_row_lbl[2], buf);
        }
        if (s_row_lbl[3]) {
            snprintf(buf, sizeof(buf), "%s " L_SPEED " %d %d%%", ICO(CMP_SPEED),
                     st->spd_lvl, st->spd_mag);
            lv_label_set_text(s_row_lbl[3], buf);
        }
        if (s_row_lbl[4]) {
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0) snprintf(buf, sizeof(buf), "%s " L_REMAIN " %d" L_HOUR "%02d" L_MIN,
                                    ICO(CMP_REMAIN), h, m);
                else       snprintf(buf, sizeof(buf), "%s " L_REMAIN " %d" L_MIN,
                                    ICO(CMP_REMAIN), m);
            } else {
                snprintf(buf, sizeof(buf), "%s " L_REMAIN " --", ICO(CMP_REMAIN));
            }
            lv_label_set_text(s_row_lbl[4], buf);
        }
    }

    // ── Page 1: AMS 霓虹色片 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;

            if (t->type[0]) {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s %d%%", i + 1, t->type, (int)t->remain);
                else       snprintf(buf, sizeof(buf), "%s %s %d%%", L_EXT, t->type, (int)t->remain);
            } else {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);

            // 色片按实时数据渲染 (透明料空心) + 当前料槽发光高亮
            if (s_ams_chip[i]) {
                ui_theme_tray_swatch(s_ams_chip[i], t);
                bool active = (i < 4) ? t->active : (st->active_tray >= 4);
                lv_obj_set_style_shadow_color(s_ams_chip[i],
                    active ? lv_color_hex(c->accent) : ui_theme_hex_color(t->color), 0);
                lv_obj_set_style_shadow_opa(s_ams_chip[i],
                    active ? LV_OPA_60 : LV_OPA_30, 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_cyber_page_count(void) { return s_total_pages; }
int style_cyber_current_page(void) { return s_page; }

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

void style_cyber_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_cyber_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
