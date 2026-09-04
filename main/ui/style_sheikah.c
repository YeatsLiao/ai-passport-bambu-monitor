// main/ui/style_sheikah.c —— 风格3: 希卡石板风（深蓝科技 + 青蓝冷光）
// 布局: Page0 = 希卡之眼 (外装饰环 + 进度弧环 + 菱形瞳孔 + 环内百分比)
//            + 状态行 + 4 行菱形符文数据 (菱形节点串在一条竖直发光线上)
//       Page1 = AMS 菱形色片列表 + 每槽发光余量细条
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

static const char *TAG __attribute__((unused)) = "style_sheikah";

#define HAS_AMS  1
#define RUNE_COUNT  4                 // Page0 符文数据行数

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

// Page 0: 希卡之眼 + 符文数据行
static lv_obj_t *s_arc       = NULL;          // 进度弧环
static lv_obj_t *s_pupil     = NULL;          // 环内菱形瞳孔 (随状态变色)
static lv_obj_t *s_pct_lbl   = NULL;          // 环内百分比
static lv_obj_t *s_state_lbl = NULL;          // 状态行 (图标 + 文字)
static int       s_rune_cmp[RUNE_COUNT];      // 每行符文对应的组件
static lv_obj_t *s_rune[RUNE_COUNT];          // 菱形节点
static lv_obj_t *s_rune_lbl[RUNE_COUNT];      // 图标 + 名称 + 值

// Page 1: AMS 列表 (0-3: AMS 槽, 4: 外挂料)
static lv_obj_t *s_ams_chip[5];               // 菱形色片
static lv_obj_t *s_ams_lbl[5];                // 类型 + 余量文字
static lv_obj_t *s_ams_bar[5];                // 余量发光细条

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

// 菱形符文: 旋转 45° 的方块, 以 (cx, cy) 为几何中心。
// pivot 必须显式设到中心, 否则默认绕左上角旋转会跑位。
// 注意: 子对象会跟随父对象一起旋转, 所以图标/文字要作为兄弟节点叠上去, 不能塞进来。
static lv_obj_t *mk_rune(lv_obj_t *parent, int cx, int cy, int size,
                         uint32_t line, uint32_t fill, lv_opa_t fill_opa) {
    if (!parent) return NULL;
    lv_obj_t *r = lv_obj_create(parent);
    if (!r) return NULL;
    int half = size / 2;
    lv_obj_set_pos(r, cx - half, cy - half);
    lv_obj_set_size(r, size, size);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(r, 0, 0);
    lv_obj_set_style_pad_all(r, 0, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(r, fill_opa, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(line), 0);
    lv_obj_set_style_shadow_color(r, lv_color_hex(line), 0);
    lv_obj_set_style_shadow_width(r, 6, 0);
    lv_obj_set_style_shadow_opa(r, LV_OPA_40, 0);
    lv_obj_set_style_transform_pivot_x(r, half, 0);
    lv_obj_set_style_transform_pivot_y(r, half, 0);
    lv_obj_set_style_transform_rotation(r, 450, 0);   // 45.0°
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

// 符文节点颜色: 温度类沿用各自仪表色, 其余用主题强调色
static uint32_t rune_color(int cmp, const ui_theme_colors_t *c) {
    switch (cmp) {
        case CMP_NOZZLE:  return c->gauge_nozzle;
        case CMP_BED:     return c->gauge_bed;
        case CMP_CHAMBER: return c->gauge_chamber;
        default:          return c->accent;
    }
}

// 组件编号 -> 当前数值文本
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
                if (h > 0) snprintf(buf, n, "%d" L_HOUR "%02d" L_MIN, h, m);
                else       snprintf(buf, n, "%d" L_MIN, m);
            } else {
                snprintf(buf, n, "--");
            }
            break;
        case CMP_SPEED:
            snprintf(buf, n, "%d %d%%", st->spd_lvl, st->spd_mag);
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
// Page 0: 希卡之眼 + 符文数据行
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
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->accent), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // 内容区 208x202, 眼心放在水平正中、上半区
    const int eye_cx = 104, eye_cy = 54;

    // ── 外侧细线装饰环 (纯装饰, 不参与数据) ──
    lv_obj_t *ring = lv_obj_create(card);
    if (ring) {
        lv_obj_set_pos(ring, eye_cx - 54, eye_cy - 54);
        lv_obj_set_size(ring, 108, 108);
        lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(c->accent), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_20, 0);
        lv_obj_set_style_pad_all(ring, 0, 0);
    }

    // ── 进度弧环: 缺口朝下, 环底青蓝冷光 ──
    s_arc = lv_arc_create(card);
    if (s_arc) {
        lv_obj_set_pos(s_arc, eye_cx - 46, eye_cy - 46);
        lv_obj_set_size(s_arc, 92, 92);
        lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_pad_all(s_arc, 0, 0);
        lv_obj_set_style_arc_width(s_arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(s_arc, 5, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(s_arc, false, LV_PART_MAIN);
        lv_obj_set_style_arc_color(s_arc, lv_color_hex(c->border), LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(s_arc, lv_color_hex(c->accent), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(s_arc, lv_color_hex(c->accent), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(s_arc, 10, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(s_arc, LV_OPA_40, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);   // 只读, 隐藏旋钮
        lv_arc_set_bg_angles(s_arc, 135, 45);
        lv_arc_set_range(s_arc, 0, 100);
        lv_arc_set_value(s_arc, 0);
    }

    // ── 环内菱形瞳孔 (希卡之眼的"眼神", 颜色随打印状态变化) ──
    s_pupil = mk_rune(card, eye_cx, eye_cy - 30, 14, c->accent, c->accent, LV_OPA_30);

    // ── 环内百分比 ──
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_BIG, c->accent);
    if (s_pct_lbl) {
        lv_obj_set_width(s_pct_lbl, 92);
        lv_label_set_long_mode(s_pct_lbl, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(s_pct_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, eye_cy - 16);
        lv_obj_set_style_shadow_color(s_pct_lbl, lv_color_hex(c->accent), 0);
        lv_obj_set_style_shadow_width(s_pct_lbl, 10, 0);
        lv_obj_set_style_shadow_opa(s_pct_lbl, LV_OPA_30, 0);
    }

    // ── 状态行 (眼下方居中) ──
    s_state_lbl = mk_lbl(card, LV_SYMBOL_WIFI " " L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_MID, 0, 112);

    // ── 4 行菱形符文, 串在一条竖直发光线上 ──
    // 百分比在眼里、状态在中区, 所以跳过它们和 AMS, 其余按配置顺序取前 4 项
    static const int fallback_cmp[RUNE_COUNT] = {CMP_LAYER, CMP_NOZZLE, CMP_BED, CMP_REMAIN};
    int n = 0;
    for (int i = 0; i < (int)s_order_len && n < RUNE_COUNT; i++) {
        int cmp = s_order[i];
        if (cmp == CMP_PERCENT || cmp == CMP_STATE || cmp == CMP_AMS) continue;
        s_rune_cmp[n++] = cmp;
    }
    for (int i = n; i < RUNE_COUNT; i++) s_rune_cmp[i] = fallback_cmp[i];

    memset(s_rune, 0, sizeof(s_rune));
    memset(s_rune_lbl, 0, sizeof(s_rune_lbl));

    const int rune_cx = 8;            // 菱形节点中心 x (留足旋转后的对角半径)
    const int rune_y0 = 137;          // 首行菱形中心 y
    const int rune_dy = 18;           // 行距

    lv_obj_t *wire = lv_obj_create(card);
    if (wire) {
        lv_obj_set_pos(wire, rune_cx, rune_y0);
        lv_obj_set_size(wire, 1, rune_dy * (RUNE_COUNT - 1));
        lv_obj_remove_flag(wire, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(wire, 0, 0);
        lv_obj_set_style_border_width(wire, 0, 0);
        lv_obj_set_style_pad_all(wire, 0, 0);
        lv_obj_set_style_bg_color(wire, lv_color_hex(c->accent), 0);
        lv_obj_set_style_bg_opa(wire, LV_OPA_30, 0);
    }

    for (int i = 0; i < RUNE_COUNT; i++) {
        int cmp = s_rune_cmp[i];
        int cy  = rune_y0 + i * rune_dy;
        // 先画菱形节点(压住竖线), 再叠文字
        s_rune[i] = mk_rune(card, rune_cx, cy, 10,
                            rune_color(cmp, c), rune_color(cmp, c), LV_OPA_40);

        char init[64];
        snprintf(init, sizeof(init), "%s %s --", ICO(cmp), cmp_name(cmp));
        s_rune_lbl[i] = mk_lbl(card, init, L_FONT_TEXT, c->text_primary);
        if (s_rune_lbl[i]) lv_obj_set_pos(s_rune_lbl[i], 20, cy - 8);
    }
}

// ---------------------------------------------------------------------------
// Page 1: AMS 菱形色片列表 + 发光余量细条
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
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(c->accent), 0);
    lv_obj_set_style_border_opa(card, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->accent);

    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));

    for (int i = 0; i < 5; i++) {
        int y = 34 + i * 34;

        // 菱形色片: 颜色由 MQTT tray_color 实时驱动 (透明料走空心描边分支)
        s_ams_chip[i] = mk_rune(card, 10, y + 9, 16,
                                c->accent, c->text_secondary, LV_OPA_COVER);

        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 26, y);

        // 余量发光细条
        lv_obj_t *bar = lv_bar_create(card);
        if (bar) {
            lv_obj_set_pos(bar, 26, y + 19);
            lv_obj_set_size(bar, 174, 3);
            lv_obj_set_style_radius(bar, 1, 0);
            lv_obj_set_style_bg_color(bar, lv_color_hex(c->border), LV_PART_MAIN);
            lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
            lv_obj_set_style_shadow_color(bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
            lv_obj_set_style_shadow_width(bar, 6, LV_PART_INDICATOR);
            lv_obj_set_style_shadow_opa(bar, LV_OPA_40, LV_PART_INDICATOR);
            lv_bar_set_range(bar, 0, 100);
            lv_bar_set_value(bar, 0, LV_ANIM_OFF);
            s_ams_bar[i] = bar;
        }
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_sheikah_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    if (s_card[0] || s_card[1]) return;   // 已创建过, 跳过

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 (持久): 青蓝发光下边框 ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(c->accent), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(header, LV_OPA_40, 0);
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

    // ── 底部栏 (持久): 青蓝发光上边框 ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(c->accent), 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_opa(footer, LV_OPA_40, 0);
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
void style_sheikah_update(void) {
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

    bool conn = bambu_mqtt_connected();
    uint32_t sc = conn ? state_color(st->state, c) : c->error;

    // ── Page 0: 希卡之眼 + 符文行 ──
    if (s_page == 0) {
        if (s_arc) lv_arc_set_value(s_arc, st->mc_percent);

        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }

        // 瞳孔与状态行同步换色, 未连接时统一用错误色
        if (s_pupil) {
            lv_obj_set_style_border_color(s_pupil, lv_color_hex(sc), 0);
            lv_obj_set_style_bg_color(s_pupil, lv_color_hex(sc), 0);
            lv_obj_set_style_shadow_color(s_pupil, lv_color_hex(sc), 0);
        }
        if (s_state_lbl) {
            if (conn) snprintf(buf, sizeof(buf), "%s %s",
                               ui_theme_state_icon(st->state, true), state_text(st->state));
            else      snprintf(buf, sizeof(buf), "%s " L_CONNECTING, LV_SYMBOL_WIFI);
            lv_label_set_text(s_state_lbl, buf);
            lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(sc), 0);
        }

        for (int i = 0; i < RUNE_COUNT; i++) {
            if (!s_rune_lbl[i]) continue;
            int cmp = s_rune_cmp[i];
            char val[24];
            cmp_value(val, sizeof(val), cmp, st);
            snprintf(buf, sizeof(buf), "%s %s %s", ICO(cmp), cmp_name(cmp), val);
            lv_label_set_text(s_rune_lbl[i], buf);

            // 状态行组件的符文节点跟随状态色, 其余保持各自语义色
            if (s_rune[i]) {
                uint32_t rc = (cmp == CMP_STATE) ? sc : rune_color(cmp, c);
                lv_obj_set_style_border_color(s_rune[i], lv_color_hex(rc), 0);
                lv_obj_set_style_bg_color(s_rune[i], lv_color_hex(rc), 0);
                lv_obj_set_style_shadow_color(s_rune[i], lv_color_hex(rc), 0);
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
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s %d%%",
                                    i + 1, t->type, (int)t->remain);
                else       snprintf(buf, sizeof(buf), "%s %s %d%%",
                                    L_EXT, t->type, (int)t->remain);
            } else {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);
            // 当前料槽高亮为强调色
            lv_obj_set_style_text_color(s_ams_lbl[i],
                lv_color_hex(t->active ? c->accent : c->text_primary), 0);

            if (s_ams_chip[i]) ui_theme_tray_swatch(s_ams_chip[i], t);
            if (s_ams_bar[i])  lv_bar_set_value(s_ams_bar[i], (int32_t)t->remain, LV_ANIM_OFF);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_sheikah_page_count(void) { return s_total_pages; }
int style_sheikah_current_page(void) { return s_page; }

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

void style_sheikah_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_sheikah_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
