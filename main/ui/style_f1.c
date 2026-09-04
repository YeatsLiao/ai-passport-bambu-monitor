// main/ui/style_f1.c —— 风格9: F1 维修墙 (Pit Wall) 风
// 视觉参照 F1 转播包装: 碳黑底 + F1 红边框条包住整屏 + 计时塔排版
//   Page0 = PIT WALL 遥测面板: LIVE 状态灯 + 会话进度条 + 容量式大字百分比
//           + 比赛时钟 + 计时塔数据行 (红色排位号 + 名称 + 右对齐读数)
//   Page1 = STANDINGS 积分榜: AMS 料槽当车队 (排位号红 + 耗材色标当车队色 + 余量条)
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

static const char *TAG __attribute__((unused)) = "style_f1";

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

// Page 0: PIT WALL 遥测面板
static lv_obj_t *s_state_dot = NULL;   // LIVE 状态灯 (运行时每秒闪烁)
static lv_obj_t *s_state_lbl = NULL;   // LIVE/状态文字
static lv_obj_t *s_prog_bar  = NULL;   // 会话进度条 (打印进度)
static lv_obj_t *s_pct_lbl   = NULL;   // 大字百分比
static lv_obj_t *s_clock_lbl = NULL;   // 比赛时钟 (剩余时间)
static lv_obj_t *s_row_lbl[5];         // 计时塔名称列
static lv_obj_t *s_row_val[5];         // 计时塔读数列 (右对齐)

// 计时塔固定行序 (印刷排版不换行序, 与工控/SSD 风同理)
static const int s_row_cmp[5] = {CMP_NOZZLE, CMP_BED, CMP_CHAMBER, CMP_LAYER, CMP_REMAIN};

// Page 1: STANDINGS 积分榜 (0-3: AMS 槽, 4: Ext 外挂)
static lv_obj_t *s_ams_chip[5];        // 车队色标 (= 耗材颜色)
static lv_obj_t *s_ams_lbl[5];         // 类型+余量文字
static lv_obj_t *s_ams_bar[5];         // 余量横条

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

// 直角纯色块 (F1 红条框/排位条/状态灯共用)
static lv_obj_t *mk_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    if (!parent) return NULL;
    lv_obj_t *b = lv_obj_create(parent);
    if (!b) return NULL;
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    return b;
}

// ---------------------------------------------------------------------------
// Page 0: PIT WALL 遥测面板
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    // 面板下探到 footer 上沿 (y286), 不留背景空带
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 面板头: 红色排位条 + PIT WALL, 右侧 LIVE 状态灯 ──
    mk_block(card, 0, 3, 4, 14, c->accent);
    lv_obj_t *ttl = mk_lbl(card, "PIT WALL", L_FONT_TEXT_BIG, c->text_primary);
    if (ttl) lv_obj_set_pos(ttl, 10, 0);
    s_state_dot = mk_block(card, 0, 0, 6, 6, c->border);
    if (s_state_dot) {
        lv_obj_align(s_state_dot, LV_ALIGN_TOP_RIGHT, 0, 6);
        lv_obj_set_style_radius(s_state_dot, LV_RADIUS_CIRCLE, 0);
    }
    s_state_lbl = mk_lbl(card, "--", L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_RIGHT, -10, 1);

    // ── 会话进度条 (打印进度, F1 红指示条) ──
    lv_obj_t *track = lv_obj_create(card);
    if (track) {
        lv_obj_set_pos(track, 0, 26);
        lv_obj_set_size(track, 204, 4);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(track, 2, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(c->border), 0);

        s_prog_bar = lv_bar_create(track);
        if (s_prog_bar) {
            lv_obj_set_size(s_prog_bar, 204, 4);
            lv_obj_align(s_prog_bar, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_radius(s_prog_bar, 2, 0);
            lv_obj_set_style_bg_opa(s_prog_bar, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_prog_bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
            lv_bar_set_range(s_prog_bar, 0, 100);
            lv_bar_set_value(s_prog_bar, 0, LV_ANIM_OFF);
        }
    }

    // ── 大字百分比 (左) + 比赛时钟 (右, 剩余时间用旗黄) ──
    // 时钟用 Montserrat 28 (无中文 fallback), 所以输出纯 ASCII 的 "1h16m"
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_HUGE, c->text_primary);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_LEFT, 0, 36);
    s_clock_lbl = mk_lbl(card, "--", L_FONT_NUM_BIG, c->warning);
    if (s_clock_lbl) lv_obj_align(s_clock_lbl, LV_ALIGN_TOP_RIGHT, 0, 52);

    // ── 计时塔数据行: 红色排位号 + 名称 + 右对齐读数 ──
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    memset(s_row_val, 0, sizeof(s_row_val));
    const char *row_names[5] = {L_NOZZLE, L_BED, L_CHAMBER, L_LAYER, L_REMAIN};
    for (int i = 0; i < 5; i++) {
        int y = 98 + i * 24;
        char buf[16];
        snprintf(buf, sizeof(buf), "0%d", i + 1);
        lv_obj_t *rank = mk_lbl(card, buf, L_FONT_NUM, c->accent);
        if (rank) lv_obj_set_pos(rank, 2, y);

        snprintf(buf, sizeof(buf), "%s %s", ICO(s_row_cmp[i]), row_names[i]);
        s_row_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_secondary);
        if (s_row_lbl[i]) lv_obj_set_pos(s_row_lbl[i], 24, y);

        // 层数/剩余时间用旗黄 (计时屏积分位), 温度读数用竞速白
        s_row_val[i] = mk_lbl(card, "--", L_FONT_TEXT,
                              (i >= 3) ? c->warning : c->text_primary);
        if (s_row_val[i]) lv_obj_align(s_row_val[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }

    // ── 面板底部红色饰线 ──
    mk_block(card, 0, 224, 204, 2, c->accent);
}

// ---------------------------------------------------------------------------
// Page 1: STANDINGS 积分榜 (AMS 当车队)
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // 面板头: 红色排位条 + STANDINGS
    mk_block(card, 0, 3, 4, 14, c->accent);
    mk_lbl(card, "STANDINGS", L_FONT_TEXT_BIG, c->text_primary);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));

    for (int i = 0; i < 5; i++) {
        int y = 36 + i * 40;

        // 红色排位号 (Ext 外挂槽显示 "EX")
        char buf[8];
        if (i < 4) snprintf(buf, sizeof(buf), "0%d", i + 1);
        else       snprintf(buf, sizeof(buf), "EX");
        lv_obj_t *rank = mk_lbl(card, buf, L_FONT_NUM, c->accent);
        if (rank) lv_obj_set_pos(rank, 2, y + 2);

        // 车队色标 = 耗材真实颜色 (收到 MQTT 数据后被覆盖)
        lv_obj_t *chip = lv_obj_create(card);
        if (chip) {
            lv_obj_set_pos(chip, 24, y + 2);
            lv_obj_set_size(chip, 14, 14);
            lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(chip, 1, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->border), 0);
            // 无数据时的占位色用主题次要文字色
            lv_obj_set_style_bg_color(chip, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_chip[i] = chip;

        // 车队行: "PLA 54%" / "(空)"
        s_ams_lbl[i] = mk_lbl(card, L_EMPTY, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 46, y);

        // 积分式余量条
        lv_obj_t *bar_bg = lv_obj_create(card);
        if (bar_bg) {
            lv_obj_set_pos(bar_bg, 24, y + 20);
            lv_obj_set_size(bar_bg, 168, 4);
            lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(bar_bg, 2, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            if (bar) {
                lv_obj_set_size(bar, 168, 4);
                lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_set_style_radius(bar, 2, 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_PART_MAIN);
                lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
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
void style_f1_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    if (s_card[0] || s_card[1]) return;   // 已创建过, 跳过

    // 碳黑底
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    const uint32_t htxt = ui_theme_on_color(c->header_bg);
    const uint32_t ftxt = ui_theme_on_color(c->footer_bg);

    // F1 红色方块 Logo + 标题 (x 从 10 起, 给左侧红竖条让位)
    lv_obj_t *logo = mk_block(header, 10, 7, 22, 16, c->accent);
    if (logo) {
        lv_obj_t *logo_txt = mk_lbl(logo, "F1", L_FONT_NUM, htxt);
        if (logo_txt) lv_obj_align(logo_txt, LV_ALIGN_CENTER, 0, 0);
    }
    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, htxt);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 40, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, htxt);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (本行无中文, 用 SYMBOL 字体)
    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, htxt);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    // ── 底部栏 (维修墙信息条) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, ftxt);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 8, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, L_FONT_NUM, ftxt);
    if (s_pg_lbl) lv_obj_align(s_pg_lbl, LV_ALIGN_RIGHT_MID, -10, 0);

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

    // 隐藏非当前页
    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == s_page) lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ── F1 红色边框条 (最后创建, 压在最上层包住整屏) ──
    mk_block(s_scr, 0, 0, 240, 2, c->accent);    // 顶边
    mk_block(s_scr, 0, 318, 240, 2, c->accent);  // 底边
    mk_block(s_scr, 0, 0, 4, 320, c->accent);    // 左侧竖条
    mk_block(s_scr, 236, 0, 4, 320, c->accent);  // 右侧竖条
}

// ---------------------------------------------------------------------------
// 更新 (每秒调用)
// ---------------------------------------------------------------------------
void style_f1_update(void) {
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

    // 电池 (图标随电量四档变化)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        // 图标颜色随电量分档 (满电绿 / 中低黄 / 低电红), 由实时数据驱动
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    bool conn = bambu_mqtt_connected();

    // ── Page 0: LIVE 灯 + 进度条 + 大字 + 计时塔 ──
    if (s_page == 0) {
        if (s_prog_bar)
            lv_bar_set_value(s_prog_bar, st->mc_percent, LV_ANIM_OFF);
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        // 比赛时钟: 剩余时间, 纯 ASCII (Montserrat 无中文 fallback)
        if (s_clock_lbl) {
            if (st->mc_remaining > 0)
                snprintf(buf, sizeof(buf), "%dh%02dm",
                         st->mc_remaining / 60, st->mc_remaining % 60);
            else
                snprintf(buf, sizeof(buf), "--");
            lv_label_set_text(s_clock_lbl, buf);
        }

        // LIVE 状态灯: 运行=红闪, 暂停=黄, 断连=红旗红, 其他=熄灭
        if (s_state_dot && s_state_lbl) {
            static bool s_live_blink = false;
            uint32_t col;
            const char *txt;
            if (!conn) {
                col = c->error;  txt = L_CONNECTING;
            } else if (st->state == BAMBU_STATE_RUNNING) {
                col = (s_live_blink = !s_live_blink) ? c->accent
                    : ((c->accent & 0xFEFEFE) >> 1);        // 暗红一拍, 形成闪烁
                txt = "LIVE";
            } else if (st->state == BAMBU_STATE_PAUSE) {
                col = c->warning;  txt = state_text(st->state);
            } else {
                col = c->border;   txt = state_text(st->state);
            }
            if (strcmp(lv_label_get_text(s_state_lbl), txt) != 0)
                lv_label_set_text(s_state_lbl, txt);
            lv_obj_set_style_bg_color(s_state_dot, lv_color_hex(col), 0);
            lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(col), 0);
        }

        // 计时塔读数列
        if (s_row_val[0]) {
            snprintf(buf, sizeof(buf), "%d/%d°C", (int)st->nozzle_temp, (int)st->nozzle_target);
            lv_label_set_text(s_row_val[0], buf);
        }
        if (s_row_val[1]) {
            snprintf(buf, sizeof(buf), "%d/%d°C", (int)st->bed_temp, (int)st->bed_target);
            lv_label_set_text(s_row_val[1], buf);
        }
        if (s_row_val[2]) {
            snprintf(buf, sizeof(buf), "%d°C", (int)st->chamber_temp);
            lv_label_set_text(s_row_val[2], buf);
        }
        if (s_row_val[3]) {
            snprintf(buf, sizeof(buf), "%d/%d", st->layer_num, st->total_layer);
            lv_label_set_text(s_row_val[3], buf);
        }
        if (s_row_val[4]) {
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0) snprintf(buf, sizeof(buf), "%d" L_HOUR "%02d" L_MIN, h, m);
                else       snprintf(buf, sizeof(buf), "%d" L_MIN, m);
            } else {
                snprintf(buf, sizeof(buf), "--");
            }
            lv_label_set_text(s_row_val[4], buf);
        }
    }

    // ── Page 1: STANDINGS 积分榜 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            if (t->type[0]) {
                if (i < 4) snprintf(buf, sizeof(buf), "%s %d%%", t->type, (int)t->remain);
                else       snprintf(buf, sizeof(buf), "%s %s %d%%", L_EXT, t->type, (int)t->remain);
            } else {
                snprintf(buf, sizeof(buf), L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);

            if (s_ams_chip[i])
                ui_theme_tray_swatch(s_ams_chip[i], t);
            if (s_ams_bar[i])
                lv_bar_set_value(s_ams_bar[i], (int32_t)t->remain, LV_ANIM_OFF);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_f1_page_count(void) { return s_total_pages; }
int style_f1_current_page(void) { return s_page; }

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

void style_f1_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_f1_prev_page(void) {
    s_page = (s_page + s_total_pages - 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
