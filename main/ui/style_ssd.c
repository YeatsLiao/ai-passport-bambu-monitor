// main/ui/style_ssd.c —— 风格8: 2.5 寸固态硬盘标签风
// 整机壳子与 2.5 寸 SSD 高度相似, UI 直接做成盘面:
//   Page0 = 盘面标签贴纸: 品牌行 + 状态徽章 + 容量式大字百分比 + 规格表 + 底部条码
//   Page1 = SMART 信息表: AMS 料槽类型/余量横条
// 顶部/底部为深灰金属端盖, 底栏嵌一颗随打印状态亮灭的"活动指示灯"
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

static const char *TAG __attribute__((unused)) = "style_ssd";

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

// Page 0: 盘面标签贴纸
static lv_obj_t *s_badge     = NULL;   // 状态徽章 (背景色随打印状态)
static lv_obj_t *s_badge_lbl = NULL;   // 徽章文字
static lv_obj_t *s_pct_lbl   = NULL;   // 容量式大字百分比
static lv_obj_t *s_row_lbl[5];         // 规格表名称列
static lv_obj_t *s_row_val[5];         // 规格表数值列 (右对齐)

// 规格表固定行序 (印刷标签不换行序, 与工控风同理)
static const int s_row_cmp[5] = {CMP_NOZZLE, CMP_BED, CMP_CHAMBER, CMP_LAYER, CMP_REMAIN};

// Page 1: SMART 信息表 (0-3: AMS 槽, 4: Ext 外挂)
static lv_obj_t *s_ams_chip[5];        // 颜色片
static lv_obj_t *s_ams_lbl[5];         // 类型+余量文字
static lv_obj_t *s_ams_bar[5];         // 余量横条

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_bat_lbl  = NULL;

// 底部栏: 活动指示灯 + 页码
static lv_obj_t *s_act_led = NULL;
static lv_obj_t *s_pg_lbl  = NULL;

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

// 小色块 (分隔线/条码条/指示灯共用): 直角纯色块
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
// Page 0: 盘面标签贴纸
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    // 标签贴纸直接下探到 footer 上沿 (y286), 整面都是盘面, 不留背景空带
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, 2, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->border), 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 品牌行: 左品牌, 右状态徽章 (SSD 标签的容量角标位置) ──
    mk_lbl(card, L_TITLE_BAMBU " Lab", L_FONT_TEXT_BIG, c->text_primary);

    s_badge = lv_obj_create(card);
    if (s_badge) {
        lv_obj_set_size(s_badge, LV_SIZE_CONTENT, 20);
        lv_obj_align(s_badge, LV_ALIGN_TOP_RIGHT, 0, 0);
        lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(s_badge, 2, 0);
        lv_obj_set_style_border_width(s_badge, 0, 0);
        lv_obj_set_style_pad_hor(s_badge, 6, 0);
        lv_obj_set_style_pad_ver(s_badge, 1, 0);
        lv_obj_set_style_bg_opa(s_badge, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(c->border), 0);
        s_badge_lbl = mk_lbl(s_badge, L_CONNECTING, L_FONT_TEXT,
                             ui_theme_on_color(c->border));
    }

    // 分隔细线 (标签印刷的横线)
    mk_block(card, 0, 26, 204, 2, c->border);

    // ── 容量式大字: 打印进度当作盘容量读数 ──
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_HUGE, c->text_primary);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_RIGHT, 0, 32);

    // ── 规格表: 名称左对齐 + 数值右对齐 (SSD 标签的规格印刷区) ──
    // 数值列右对齐: 宽度变化向左扩展, 不会撞到名称列
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    memset(s_row_val, 0, sizeof(s_row_val));
    const char *row_names[5] = {L_NOZZLE, L_BED, L_CHAMBER, L_LAYER, L_REMAIN};
    for (int i = 0; i < 5; i++) {
        int y = 96 + i * 24;
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %s", ICO(s_row_cmp[i]), row_names[i]);
        s_row_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_secondary);
        if (s_row_lbl[i]) lv_obj_set_pos(s_row_lbl[i], 0, y);
        s_row_val[i] = mk_lbl(card, "--", L_FONT_TEXT, c->text_primary);
        if (s_row_val[i]) lv_obj_align(s_row_val[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }

    // ── 底部条码装饰 + 序列号 (SSD 标签的条形码区) ──
    static const uint8_t bc_w[] = {6,2,2,4,2,6,4,2,2,2,8,2,4,2,2,6,2,4};
    int bx = 0;
    for (int i = 0; i < (int)(sizeof(bc_w) / sizeof(bc_w[0])); i++) {
        if (!mk_block(card, bx, 212, bc_w[i], 16, c->text_primary)) break;
        bx += bc_w[i] + 2;
    }
    lv_obj_t *sn = mk_lbl(card, "S/N BMBU-MON-25", L_FONT_NUM, c->text_secondary);
    if (sn) lv_obj_align(sn, LV_ALIGN_TOP_RIGHT, 0, 216);
}

// ---------------------------------------------------------------------------
// Page 1: SMART 信息表 (AMS 料槽)
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

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS " SMART", L_FONT_TEXT_BIG, c->accent);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));

    for (int i = 0; i < 5; i++) {
        int y = 36 + i * 40;
        // 颜色片 (贴纸上的耗材色标)
        lv_obj_t *chip = lv_obj_create(card);
        if (chip) {
            lv_obj_set_pos(chip, 0, y + 2);
            lv_obj_set_size(chip, 14, 14);
            lv_obj_remove_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(chip, 1, 0);
            lv_obj_set_style_border_width(chip, 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->border), 0);
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(chip, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_chip[i] = chip;

        // 文字: "#1 PLA 54%"
        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 22, y);

        // 余量横条 (SMART 表的健康度条样式)
        lv_obj_t *bar_bg = lv_obj_create(card);
        if (bar_bg) {
            lv_obj_set_pos(bar_bg, 22, y + 20);
            lv_obj_set_size(bar_bg, 170, 5);
            lv_obj_remove_flag(bar_bg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(bar_bg, 2, 0);
            lv_obj_set_style_border_width(bar_bg, 0, 0);
            lv_obj_set_style_pad_all(bar_bg, 0, 0);
            lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);

            lv_obj_t *bar = lv_bar_create(bar_bg);
            if (bar) {
                lv_obj_set_size(bar, 170, 5);
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
void style_ssd_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    if (s_card[0] || s_card[1]) return;   // 已创建过, 跳过

    // 壳体背景
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 (顶部金属端盖) ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // 端盖/标签文字颜色按各自背景亮度自动取黑/白, 不硬编码
    const uint32_t htxt = ui_theme_on_color(c->header_bg);
    const uint32_t ftxt = ui_theme_on_color(c->footer_bg);

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, htxt);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, htxt);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (本行无中文, 用 SYMBOL 字体)
    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, htxt);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (底部金属端盖 + 活动指示灯) ──
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

    // 活动指示灯: SSD 盘面的状态 LED, 打印运行时闪烁, 空闲熄灭
    s_act_led = mk_block(footer, 0, 0, 6, 6, c->border);
    if (s_act_led) {
        lv_obj_align(s_act_led, LV_ALIGN_RIGHT_MID, -38, 0);
        lv_obj_set_style_radius(s_act_led, LV_RADIUS_CIRCLE, 0);
    }

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
void style_ssd_update(void) {
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

    // ── Page 0: 徽章 + 大字 + 规格表 ──
    if (s_page == 0) {
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }

        // 状态徽章: 背景色 = 状态语义色, 文字色按背景亮度自动取黑/白
        if (s_badge && s_badge_lbl) {
            uint32_t bgc = conn ? state_color(st->state, c) : c->error;
            const char *txt = conn ? state_text(st->state) : L_CONNECTING;
            if (strcmp(lv_label_get_text(s_badge_lbl), txt) != 0)
                lv_label_set_text(s_badge_lbl, txt);
            lv_obj_set_style_bg_color(s_badge, lv_color_hex(bgc), 0);
            lv_obj_set_style_text_color(s_badge_lbl, lv_color_hex(ui_theme_on_color(bgc)), 0);
        }

        // 规格表数值列
        if (s_row_val[0])
            snprintf(buf, sizeof(buf), "%d/%d°C", (int)st->nozzle_temp, (int)st->nozzle_target);
        if (s_row_val[0]) lv_label_set_text(s_row_val[0], buf);
        if (s_row_val[1])
            snprintf(buf, sizeof(buf), "%d/%d°C", (int)st->bed_temp, (int)st->bed_target);
        if (s_row_val[1]) lv_label_set_text(s_row_val[1], buf);
        if (s_row_val[2])
            snprintf(buf, sizeof(buf), "%d°C", (int)st->chamber_temp);
        if (s_row_val[2]) lv_label_set_text(s_row_val[2], buf);
        if (s_row_val[3])
            snprintf(buf, sizeof(buf), "%d/%d", st->layer_num, st->total_layer);
        if (s_row_val[3]) lv_label_set_text(s_row_val[3], buf);
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

    // ── Page 1: SMART 信息表 ──
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

            if (s_ams_chip[i])
                ui_theme_tray_swatch(s_ams_chip[i], t);
            if (s_ams_bar[i])
                lv_bar_set_value(s_ams_bar[i], (int32_t)t->remain, LV_ANIM_OFF);
        }
    }

    // ── 活动指示灯 (每秒翻转): 运行=绿闪, 暂停=黄, 断连=红, 空闲=熄灭 ──
    if (s_act_led) {
        static bool s_act_blink = false;
        uint32_t col;
        if (!conn)                          col = c->error;
        else if (st->state == BAMBU_STATE_RUNNING)
            col = (s_act_blink = !s_act_blink) ? c->success
                : ((c->success & 0xFEFEFE) >> 1);           // 暗绿一拍, 形成闪烁
        else if (st->state == BAMBU_STATE_PAUSE) col = c->warning;
        else                                col = c->border;     // 熄灭: 暗金属色
        lv_obj_set_style_bg_color(s_act_led, lv_color_hex(col), 0);
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_ssd_page_count(void) { return s_total_pages; }
int style_ssd_current_page(void) { return s_page; }

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

void style_ssd_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_ssd_prev_page(void) {
    s_page = (s_page + s_total_pages - 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
