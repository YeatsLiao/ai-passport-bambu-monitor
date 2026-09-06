// main/ui/style_f1.c —— 风格9: F1 转播计时风 (Timing Tower)
// 视觉参照 F1 watchOS 转播包装: 近黑底上悬浮圆角暗卡 + 车队涂装色条 + F1 红点缀
//   Page0 = LAP 计时塔: 红竖线+LAP+旗黄大字百分比 + 比赛时钟 + LIVE 状态灯
//           + 进度条 + 5 张圆角行卡 (涂装色条 + 彩色排位号 + 名称 + 右对齐读数)
//   Page1 = STANDINGS 积分榜: AMS 料槽当车队 (耗材真实颜色当涂装色条 + 余量条)
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

// Page 0: LAP 计时塔
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

// 车队涂装色条 (装饰性识别色, 仅用于计时塔行卡; 数据颜色仍由 MQTT 实时驱动)
static const uint32_t F1_LIVERY[5] = {
    0x00D2BE,   // Mercedes 青绿
    0xDC0000,   // Ferrari 红
    0x1E41FF,   // Red Bull 蓝
    0xFF8700,   // McLaren 橙
    0xF596C8,   // Racing Point 粉
};

// 悬浮圆角行卡 (转播计时塔的暗卡; clip_corner 让色条跟随圆角; stripe=0 表示不画)
static lv_obj_t *mk_row_card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t stripe) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *row = lv_obj_create(parent);
    if (!row) return NULL;
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, w, h);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_clip_corner(row, true, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(c->card_bg), 0);
    if (stripe) mk_block(row, 0, 0, 3, h, stripe);
    return row;
}

// ---------------------------------------------------------------------------
// Page 0: LAP 计时塔
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    // 页容器透明: 行卡直接悬浮在碳黑底上 (转播包装的排版方式)
    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 顶部: 红竖线 + LAP + 旗黄大字百分比 (转播 "LAP 11/55" 位) ──
    mk_block(card, 4, 2, 3, 24, c->accent);
    lv_obj_t *lap = mk_lbl(card, "LAP", L_FONT_TEXT_BIG, c->text_primary);
    if (lap) lv_obj_set_pos(lap, 14, 0);
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_BIG, c->warning);
    if (s_pct_lbl) lv_obj_set_pos(s_pct_lbl, 64, 0);

    // 右上比赛时钟 (旗黄, Montserrat 无中文 fallback, 输出纯 ASCII "1h16m";
    // 用 NUM 字号避免与大字百分比在 224 宽内相撞)
    s_clock_lbl = mk_lbl(card, "--", L_FONT_NUM, c->warning);
    if (s_clock_lbl) lv_obj_align(s_clock_lbl, LV_ALIGN_TOP_RIGHT, -4, 4);

    // LIVE 状态灯 + 状态文字 (时钟下方; 文字含图标+3汉字约 72px, 圆点在其左侧留 8px)
    s_state_dot = mk_block(card, 0, 0, 6, 6, c->border);
    if (s_state_dot) {
        lv_obj_align(s_state_dot, LV_ALIGN_TOP_RIGHT, -80, 35);
        lv_obj_set_style_radius(s_state_dot, LV_RADIUS_CIRCLE, 0);
    }
    s_state_lbl = mk_lbl(card, "--", L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_RIGHT, -4, 30);

    // ── 会话进度条 (打印进度, F1 红指示条) ──
    lv_obj_t *track = lv_obj_create(card);
    if (track) {
        lv_obj_set_pos(track, 4, 50);
        lv_obj_set_size(track, 216, 4);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(track, 2, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(c->border), 0);

        s_prog_bar = lv_bar_create(track);
        if (s_prog_bar) {
            lv_obj_set_size(s_prog_bar, 216, 4);
            lv_obj_align(s_prog_bar, LV_ALIGN_LEFT_MID, 0, 0);
            lv_obj_set_style_radius(s_prog_bar, 2, 0);
            lv_obj_set_style_bg_opa(s_prog_bar, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_prog_bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
            lv_bar_set_range(s_prog_bar, 0, 100);
            lv_bar_set_value(s_prog_bar, 0, LV_ANIM_OFF);
        }
    }

    // ── 计时塔行卡: 涂装色条 + 彩色排位号 + 名称 + 右对齐读数 ──
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    memset(s_row_val, 0, sizeof(s_row_val));
    const char *row_names[5] = {L_NOZZLE, L_BED, L_CHAMBER, L_LAYER, L_REMAIN};
    for (int i = 0; i < 5; i++) {
        int y = 58 + i * 39;
        lv_obj_t *row = mk_row_card(card, 4, y, 216, 33, F1_LIVERY[i]);
        if (!row) continue;

        char buf[16];
        snprintf(buf, sizeof(buf), "0%d", i + 1);
        lv_obj_t *rank = mk_lbl(row, buf, L_FONT_NUM, F1_LIVERY[i]);   // 排位号用车队色
        if (rank) lv_obj_set_pos(rank, 10, 8);

        snprintf(buf, sizeof(buf), "%s %s", ICO(s_row_cmp[i]), row_names[i]);
        s_row_lbl[i] = mk_lbl(row, buf, L_FONT_TEXT, c->text_primary);
        if (s_row_lbl[i]) lv_obj_set_pos(s_row_lbl[i], 36, 8);

        // 层数/剩余时间用旗黄 (计时屏积分位), 温度读数用竞速白
        s_row_val[i] = mk_lbl(row, "--", L_FONT_TEXT,
                              (i >= 3) ? c->warning : c->text_primary);
        if (s_row_val[i]) lv_obj_align(s_row_val[i], LV_ALIGN_RIGHT_MID, -10, 0);
    }
}

// ---------------------------------------------------------------------------
// Page 1: STANDINGS 积分榜 (AMS 当车队)
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    // 页容器透明: 行卡直接悬浮在碳黑底上
    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 面板头: 红竖线 + STANDINGS (转播 Drivers 榜位) ──
    mk_block(card, 4, 2, 3, 14, c->accent);
    lv_obj_t *ttl = mk_lbl(card, "STANDINGS", L_FONT_TEXT_BIG, c->text_primary);
    if (ttl) lv_obj_set_pos(ttl, 14, 0);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));

    // ── 车队行卡: 耗材真实颜色当涂装色条 + 排位号 + 类型余量 + 底部细余量条 ──
    for (int i = 0; i < 5; i++) {
        int y = 30 + i * 43;
        lv_obj_t *row = mk_row_card(card, 4, y, 216, 38, 0);
        if (!row) continue;

        // 涂装色条位 = 耗材真实颜色 (update 时由 ui_theme_tray_swatch 上色,
        // 透明耗材走空心描边逻辑, 未知时先用主题次要文字色占位)
        s_ams_chip[i] = mk_block(row, 0, 0, 3, 38, c->text_secondary);

        // 排位号 (Ext 外挂槽显示 "EX")
        char buf[8];
        if (i < 4) snprintf(buf, sizeof(buf), "0%d", i + 1);
        else       snprintf(buf, sizeof(buf), "EX");
        lv_obj_t *rank = mk_lbl(row, buf, L_FONT_NUM, c->text_primary);
        if (rank) lv_obj_set_pos(rank, 10, 11);

        // 车队行: "PLA 54%" / "(空)"
        s_ams_lbl[i] = mk_lbl(row, L_EMPTY, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 36, 3);

        // 积分式余量条 (卡片底部细条, F1 红指示)
        lv_obj_t *bar_bg = lv_obj_create(row);
        if (bar_bg) {
            lv_obj_set_pos(bar_bg, 36, 31);
            lv_obj_set_size(bar_bg, 170, 3);
            lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(bar_bg, 1, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            if (bar) {
                lv_obj_set_size(bar, 170, 3);
                lv_obj_align(bar, LV_ALIGN_LEFT_MID, 0, 0);
                lv_obj_set_style_radius(bar, 1, 0);
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
