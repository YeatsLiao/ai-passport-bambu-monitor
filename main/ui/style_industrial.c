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

// LED 状态灯 (lv_led 组件: 熄灭时内部自动混黑, 点亮时按设定色发光)
//
// lv_led 的绘制是两次 lv_color_mix: 先用样式 bg_color 的亮度混合 led 色, 再按
// brightness 混黑。把 bg_color 固定成白色可让第一步不损失原色, 最终明暗完全交给
// lv_led_set_brightness —— 这样"熄灭"是真的变暗, 而不是换成另一种颜色。
static lv_obj_t *mk_led(lv_obj_t *parent, int x, int y, uint32_t color) {
    if (!parent) return NULL;
    lv_obj_t *d = lv_led_create(parent);
    if (!d) return NULL;
    lv_obj_set_pos(d, x, y);
    lv_obj_set_size(d, 8, 8);          // lv_led 无 set_size 接口, 走通用对象接口
    lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, lv_color_white(), 0);
    lv_obj_set_style_shadow_width(d, 8, 0);   // 光晕宽度由 lv_led 按亮度缩放
    lv_obj_set_style_shadow_spread(d, 0, 0);
    lv_obj_set_style_shadow_opa(d, LV_OPA_50, 0);
    lv_led_set_color(d, lv_color_hex(color));
    lv_led_off(d);
    return d;
}

// lv_led_set_color 会无条件标脏, 20 段每秒重设代价可观; 颜色未变则跳过
static void led_set_color(lv_obj_t *led, uint32_t color) {
    if (!led) return;
    lv_color_t want = lv_color_hex(color);
    if (lv_color_eq(lv_led_get_color(led), want)) return;
    lv_led_set_color(led, want);
}

// 设定状态灯颜色并点亮
static void led_on(lv_obj_t *led, uint32_t color) {
    if (!led) return;
    led_set_color(led, color);
    lv_led_on(led);
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

// 加热中灯色 (调用前已确保 target > 0): 低于目标 5°C 以上 = 黄色(升温中), 否则绿色(已到温)
static uint32_t heat_led_color(float cur, float target, const ui_theme_colors_t *c) {
    if (cur < target - 5.0f) return c->warning;
    return c->success;
}

// 组件编号 -> 本地化短名 (中英文由 ui_lang.h 的 CFG_LANG 决定)
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

// 数据行对应的组件 (build/update 共用, 保证图标与内容一致)
static const int s_row_cmp[5] = {CMP_NOZZLE, CMP_BED, CMP_CHAMBER, CMP_LAYER, CMP_STATE};

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

    // ── LED 段式进度条 (20 段 lv_led, 9x14, 间隔 1px) ──
    // 工控风用方角 (radius 1) 且关掉光晕: 20 个发光对象每秒重绘在无 PSRAM 的
    // C3 上代价太高, 只给 5 个状态灯保留光晕。
    memset(s_led_seg, 0, sizeof(s_led_seg));
    for (int i = 0; i < LED_SEG_COUNT; i++) {
        lv_obj_t *seg = lv_led_create(card);
        if (!seg) continue;
        lv_obj_set_pos(seg, i * 10, 4);
        lv_obj_set_size(seg, 9, 14);
        lv_obj_remove_flag(seg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(seg, 1, 0);
        lv_obj_set_style_border_width(seg, 0, 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(seg, lv_color_white(), 0);   // 明暗交给亮度控制
        lv_obj_set_style_shadow_width(seg, 0, 0);
        lv_led_set_color(seg, lv_color_hex(c->success));
        lv_led_off(seg);                                        // 初始全熄灭
        s_led_seg[i] = seg;
    }

    // ── 大字百分比 (居中) + 剩余时间 (右侧) ──
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_HUGE, c->success);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, -20, 26);
    // 右对齐: 中英文宽度变化向左扩展, 不会撞到百分比
    char rinit[32];
    snprintf(rinit, sizeof(rinit), "%s --", ICO(CMP_REMAIN));
    s_remain_lbl = mk_lbl(card, rinit, L_FONT_TEXT, c->text_secondary);
    if (s_remain_lbl) lv_obj_align(s_remain_lbl, LV_ALIGN_TOP_RIGHT, -4, 44);

    // ── 数据行网格: NOZ / BED / CHM / LAYER / STATE ──
    memset(s_row_dot, 0, sizeof(s_row_dot));
    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    // "%-4s" 那类空格对齐对中文无效 (CJK 按字形宽度算), 统一用 "图标 + 名称 + 值"
    const char *inits[5] = {"--/--°C", "--/--°C", "--°C", "--/--", L_CONNECTING};
    for (int i = 0; i < 5; i++) {
        s_row_dot[i] = mk_led(card, 2, 96 + i * 24, c->border);
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s %s",
                 ICO(s_row_cmp[i]), cmp_name(s_row_cmp[i]), inits[i]);
        // 标题与值合并为单标签, 前置 LED 灯负责状态色
        s_row_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
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

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->accent);

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
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(chip, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_chip[i] = chip;

        // 文字: "#1 PLA 54%"
        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
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
void style_industrial_update(void) {
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

    // ── Page 0: LED 条 + 大字 + 数据行 ──
    if (s_page == 0) {
        // LED 段: 点亮 pct*20/100 段; 末端一段混白做高光 (借鉴 BambuHelper)
        int lit = st->mc_percent * LED_SEG_COUNT / 100;
        for (int i = 0; i < LED_SEG_COUNT; i++) {
            if (!s_led_seg[i]) continue;
            if (i >= lit) { lv_led_off(s_led_seg[i]); continue; }
            uint32_t col = (i == lit - 1)
                         ? ((c->success & 0xFEFEFE) + 0x7F7F7F) >> 1   // 末端: 绿混白
                         : c->success;
            led_set_color(s_led_seg[i], col);
            lv_led_on(s_led_seg[i]);
        }

        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        if (s_remain_lbl) {
            if (st->mc_remaining > 0) {
                int h = st->mc_remaining / 60;
                int m = st->mc_remaining % 60;
                if (h > 0) snprintf(buf, sizeof(buf), "%s %d" L_HOUR "%02d" L_MIN,
                                    ICO(CMP_REMAIN), h, m);
                else       snprintf(buf, sizeof(buf), "%s %d" L_MIN, ICO(CMP_REMAIN), m);
            } else {
                snprintf(buf, sizeof(buf), "%s --", ICO(CMP_REMAIN));
            }
            lv_label_set_text(s_remain_lbl, buf);
        }

        // 数据行 (图标 + 本地化名 + 值; 前置 LED 灯反映加热/运行状态)
        if (s_row_lbl[0]) {
            snprintf(buf, sizeof(buf), "%s %s %d/%d°C", ICO(CMP_NOZZLE), L_NOZZLE,
                     (int)st->nozzle_temp, (int)st->nozzle_target);
            lv_label_set_text(s_row_lbl[0], buf);
        }
        // 喷嘴/热床: 只有设了加热目标才亮灯, 冷机时熄灭 (否则灯恒亮就没有信息量了)
        if (s_row_dot[0]) {
            if (st->nozzle_target > 0.0f)
                led_on(s_row_dot[0], heat_led_color(st->nozzle_temp, st->nozzle_target, c));
            else
                lv_led_off(s_row_dot[0]);
        }

        if (s_row_lbl[1]) {
            snprintf(buf, sizeof(buf), "%s %s %d/%d°C", ICO(CMP_BED), L_BED,
                     (int)st->bed_temp, (int)st->bed_target);
            lv_label_set_text(s_row_lbl[1], buf);
        }
        if (s_row_dot[1]) {
            if (st->bed_target > 0.0f)
                led_on(s_row_dot[1], heat_led_color(st->bed_temp, st->bed_target, c));
            else
                lv_led_off(s_row_dot[1]);
        }

        if (s_row_lbl[2]) {
            snprintf(buf, sizeof(buf), "%s %s %d°C", ICO(CMP_CHAMBER), L_CHAMBER,
                     (int)st->chamber_temp);
            lv_label_set_text(s_row_lbl[2], buf);
        }
        // 腔体无加热目标: 有余热 (>30°C) 时黄灯提示, 否则熄灭
        if (s_row_dot[2]) {
            if (st->chamber_temp > 30.0f) led_on(s_row_dot[2], c->warning);
            else                          lv_led_off(s_row_dot[2]);
        }

        if (s_row_lbl[3]) {
            snprintf(buf, sizeof(buf), "%s %s %d/%d", ICO(CMP_LAYER), L_LAYER,
                     st->layer_num, st->total_layer);
            lv_label_set_text(s_row_lbl[3], buf);
        }
        // 层数灯: 只在真正逐层推进时点亮
        if (s_row_dot[3]) {
            if (st->state == BAMBU_STATE_RUNNING) led_on(s_row_dot[3], c->accent);
            else                                  lv_led_off(s_row_dot[3]);
        }

        if (s_row_lbl[4]) {
            if (bambu_mqtt_connected()) {
                snprintf(buf, sizeof(buf), "%s %s %s", ui_theme_state_icon(st->state, true),
                         L_STATE, state_text(st->state));
                lv_label_set_text(s_row_lbl[4], buf);
                led_on(s_row_dot[4], state_color(st->state, c));
            } else {
                snprintf(buf, sizeof(buf), "%s %s %s", LV_SYMBOL_WIFI, L_STATE, L_CONNECTING);
                lv_label_set_text(s_row_lbl[4], buf);
                led_on(s_row_dot[4], c->error);
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
