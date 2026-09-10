// main/ui/style_apple.c —— 风格12: Apple 风（iOS 浅色分组列表 + systemBlue 强调）
//
// 设计语言 (Apple HIG / apple-design skill):
//   - iOS Settings 的 inset grouped list: 浅灰底 + 白色圆角卡片, 层级靠卡片分组而非边框
//   - Section header 放卡片外 (小号灰字), 卡片内分隔线与文字左对齐内缩 (iOS 细节)
//   - systemBlue 强调色只给活动位置; 图标沿用 iOS Settings 的语义色 (橙喷嘴/蓝热床/紫腔体)
//   - 数据驱动配色: 状态/电量/耗材颜色全部来自 MQTT 实时数据
//   - 克制动画: 翻页 220ms 淡入 + 12px 上移 ease-out (近似 iOS 临界阻尼入场, 无回弹)
//
// 翻页策略: 两页容器同时创建, 翻页只切换显示/隐藏 + 入场动画 (不 destroy/rebuild)
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

static const char *TAG __attribute__((unused)) = "style_apple";

#define CMP_NOZZLE 1
#define CMP_BED 2
#define CMP_CHAMBER 3
#define CMP_LAYER 4
#define CMP_PERCENT 5
#define CMP_REMAIN 6
#define CMP_STATE 7
#define CMP_SPEED 8

// 组件图标统一由 ui_theme 映射, 保证各风格语义一致
#define ICO(cmp) ui_theme_component_icon(cmp)

#ifndef CFG_COMPONENT_ORDER
static const int s_default_order[] = {5, 4, 1, 2, 7, 8};
#define s_order s_default_order
#define s_order_len 6
#else
static const int s_order[] = CFG_COMPONENT_ORDER;
#define s_order_len (sizeof(s_order) / sizeof(s_order[0]))
#endif

// ── 布局常量 (240x320, iOS inset grouped 尺度) ──
#define CARD_X       12
#define CARD_W       216    // 左右各留 12pt 边距 (iOS inset grouped)
#define CARD_Y       34
#define CARD_H       252    // 下探到 footer 上沿 (y286), 与其它风格一致
#define ROW_H        25     // 数据行高
#define SEP_INSET    14     // 卡片内分隔线左内缩 (与文字对齐, iOS 细节)

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 2;

// 两页容器 (翻页时只切换可见性)
static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0 组件
static lv_obj_t *s_pct_lbl = NULL;    // 大数字进度 (视觉主角)
static lv_obj_t *s_bar = NULL;        // 圆角进度条
static lv_obj_t *s_row_lbl[6];        // 行左标签 (图标 + 本地化文字)
static lv_obj_t *s_row_val[6];        // 行右数值 (右对齐)

// Page 1 AMS (索引 0-3: AMS 料槽, 4: 外挂料槽 Ext)
static lv_obj_t *s_ams_lbl[5];        // 左: "#N TYPE"
static lv_obj_t *s_ams_val[5];        // 右: "87%" (右对齐)
static lv_obj_t *s_ams_bar[5];        // 行底圆角余量条
static lv_obj_t *s_ams_swatch[5];     // 颜色色块 (颜色来自 MQTT tray_color)

// 标题栏标签
static lv_obj_t *s_time_lbl = NULL;   // 实时时间 (NTP 同步, HH:MM, iOS 状态栏位)
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

// 行数值标签: 右对齐 + 固定行 y (LVGL 9 对齐是样式属性, 文字变宽后仍保持右贴)
// x 预留 SEP_INSET 内边距, 不让文字顶到卡片右缘 (实机验证: 贴缘会被裁掉)
static void mk_row_right(lv_obj_t *l, lv_coord_t y) {
    if (!l) return;
    lv_obj_set_align(l, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_x(l, -SEP_INSET);
    lv_obj_set_y(l, y);
}

// 白色 inset 圆角卡片 (iOS grouped list 容器, 无边框, 层级靠与浅灰底的明度差)
static lv_obj_t *mk_card(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    if (!p) return NULL;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(p);
    if (!card) return NULL;
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return card;
}

// section header: 卡片外小号灰字 (iOS Settings 的分组标题位)
static void mk_section(lv_obj_t *p, const char *t, lv_coord_t x, lv_coord_t y) {
    if (!p) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *l = mk_lbl(p, t, L_FONT_TEXT, c->text_secondary);
    if (l) lv_obj_set_pos(l, x, y);
}

// 透明度动画执行回调 (lv_anim exec cb 只接受 (var, int32_t) 签名)
static void opa_anim_cb(void *var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

// 入场动画: 淡入 + 12px 上移, 220ms ease-out
// (Apple: 临界阻尼无回弹; 从 opacity 0 + 微位移出现, 入场起步快)
static void anim_enter(lv_obj_t *card) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, CARD_Y + 12, CARD_Y);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);

    lv_anim_set_exec_cb(&a, opa_anim_cb);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_start(&a);
}

// iOS 圆角细进度条 (轨道 systemGray5, 填充随语义色)
static lv_obj_t *mk_round_bar(lv_obj_t *p, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    if (!p) return NULL;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *bar = lv_bar_create(p);
    if (!bar) return NULL;
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c->border), 0);   // 轨道 = 分隔灰
    lv_obj_set_style_radius(bar, h / 2, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, h / 2, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    return bar;
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
// 构建 Page 0: 主状态 (创建为 s_content_area 的子对象)
// 布局: [PROGRESS header] [hero 卡片: 大数字 + 圆角条]
//       [STATUS header]  [分组列表卡片: 左图标标签 / 右值, 分隔线内缩]
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *page = lv_obj_create(s_content_area);
    if (!page) return;
    s_card[0] = page;

    lv_obj_set_pos(page, 0, CARD_Y);
    lv_obj_set_size(page, 240, CARD_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    // ── PROGRESS 分组 ──
    mk_section(page, L_PROGRESS, CARD_X + 2, 0);

    lv_obj_t *hero = mk_card(page, CARD_X, 16, CARD_W, 88);
    if (hero) {
        s_pct_lbl = mk_lbl(hero, "--%", L_FONT_NUM_HUGE, c->text_primary);
        if (s_pct_lbl) lv_obj_set_pos(s_pct_lbl, SEP_INSET, 6);

        s_bar = mk_round_bar(hero, SEP_INSET, 66, CARD_W - SEP_INSET * 2, 4);
    }

    // ── STATUS 分组: 数据行列表 (order 中除 PERCENT 外的组件) ──
    // header 底部与卡片顶部留足空隙 (实机验证: 14pt 行高 ~19px, 贴太近会叠在卡片上)
    mk_section(page, L_STATE, CARD_X + 2, 108);

    // 行数决定列表卡片高度 (5 行 x 25 = 125, y126 起刚好收在 252 内)
    int rows = 0;
    for (int i = 0; i < s_order_len; i++) {
        if (s_order[i] != CMP_PERCENT) rows++;
    }
    if (rows > 5) rows = 5;
    if (rows < 1) rows = 1;

    lv_coord_t list_h = rows * ROW_H;
    lv_obj_t *list = mk_card(page, CARD_X, 126, CARD_W, list_h);
    if (!list) return;

    memset(s_row_lbl, 0, sizeof(s_row_lbl));
    memset(s_row_val, 0, sizeof(s_row_val));

    int row = 0;
    for (int i = 0; i < s_order_len && row < 6; i++) {
        int cmp = s_order[i];
        if (cmp == CMP_PERCENT) continue;

        char buf[48];
        switch (cmp) {
            case CMP_LAYER:
                snprintf(buf, sizeof(buf), "%s " L_LAYER, ICO(CMP_LAYER)); break;
            case CMP_NOZZLE:
                snprintf(buf, sizeof(buf), "%s " L_NOZZLE, ICO(CMP_NOZZLE)); break;
            case CMP_BED:
                snprintf(buf, sizeof(buf), "%s " L_BED, ICO(CMP_BED)); break;
            case CMP_CHAMBER:
                snprintf(buf, sizeof(buf), "%s " L_CHAMBER, ICO(CMP_CHAMBER)); break;
            case CMP_REMAIN:
                snprintf(buf, sizeof(buf), "%s " L_REMAIN, ICO(CMP_REMAIN)); break;
            case CMP_STATE:
                snprintf(buf, sizeof(buf), "%s " L_STATE, LV_SYMBOL_WIFI); break;
            case CMP_SPEED:
                snprintf(buf, sizeof(buf), "%s " L_SPEED, ICO(CMP_SPEED)); break;
            default: continue;
        }

        lv_coord_t row_y = row * ROW_H + 5;

        s_row_lbl[row] = mk_lbl(list, buf, L_FONT_TEXT, c->text_secondary);
        if (s_row_lbl[row]) {
            lv_obj_set_pos(s_row_lbl[row], SEP_INSET, row_y);
        }

        s_row_val[row] = mk_lbl(list, "--", L_FONT_NUM, c->text_primary);
        mk_row_right(s_row_val[row], row_y);

        // 分隔线: 与文字左对齐内缩, 不顶到卡片右缘 (iOS 细节), 最后一行不画
        // 用 secondary 灰 30% 透明度, 纯 systemGray5 在白卡上几乎不可见 (实机验证)
        if (row < rows - 1) {
            lv_obj_t *sep = lv_obj_create(list);
            if (sep) {
                lv_obj_set_pos(sep, SEP_INSET, (row + 1) * ROW_H - 1);
                lv_obj_set_size(sep, CARD_W - SEP_INSET * 2, 1);
                lv_obj_set_style_bg_color(sep, lv_color_hex(c->text_secondary), 0);
                lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
                lv_obj_set_style_radius(sep, 0, 0);
                lv_obj_set_style_border_width(sep, 0, 0);
                lv_obj_set_style_pad_all(sep, 0, 0);
                lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
        }
        row++;
    }
}

// ---------------------------------------------------------------------------
// 构建 Page 1: AMS (创建为 s_content_area 的子对象)
// 每行: 色块 + "#N TYPE" + 右对齐余量% + 行底圆角余量条
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *page = lv_obj_create(s_content_area);
    if (!page) return;
    s_card[1] = page;

    lv_obj_set_pos(page, 0, CARD_Y);
    lv_obj_set_size(page, 240, CARD_H);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    // 卡片 y24 起: 给 19px 高的 section header 留出空隙, 不与卡片重叠 (同 page0 教训)
    mk_section(page, L_AMS, CARD_X + 2, 0);

    lv_obj_t *list = mk_card(page, CARD_X, 24, CARD_W, 228);
    if (!list) return;

    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_val, 0, sizeof(s_ams_val));
    memset(s_ams_bar, 0, sizeof(s_ams_bar));
    memset(s_ams_swatch, 0, sizeof(s_ams_swatch));

    for (int i = 0; i < 5; i++) {
        // 行内节奏统一: 文字 -> 8px -> 余量条 -> 14px -> 分隔线 -> 7px -> 下一行
        lv_coord_t y = 10 + i * 44;

        // 色块 (颜色在 update 中由 MQTT 推送的 tray_color 填充)
        // 描边用 secondary 灰: systemGray5 描边在白卡上挡不住浅色耗材 (实机验证 #2 PLA 隐形)
        lv_obj_t *sw = lv_obj_create(list);
        if (sw) {
            lv_obj_set_size(sw, 14, 14);
            lv_obj_set_pos(sw, SEP_INSET, y + 2);
            lv_obj_remove_flag(sw, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(sw, 4, 0);
            lv_obj_set_style_border_width(sw, 1, 0);
            lv_obj_set_style_border_color(sw, lv_color_hex(c->text_secondary), 0);
            // 无数据时的占位色用主题次要文字色 (收到 MQTT 数据后被覆盖)
            lv_obj_set_style_bg_color(sw, lv_color_hex(c->text_secondary), 0);
        }
        s_ams_swatch[i] = sw;

        s_ams_lbl[i] = mk_lbl(list, L_EMPTY, L_FONT_TEXT, c->text_secondary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], SEP_INSET + 22, y);

        s_ams_val[i] = mk_lbl(list, "--", L_FONT_NUM, c->text_primary);
        mk_row_right(s_ams_val[i], y);

        s_ams_bar[i] = mk_round_bar(list, SEP_INSET, y + 24, CARD_W - SEP_INSET * 2, 3);

        // 分隔线: 与文字左对齐内缩, 最后一行不画 (实机验证: 用 secondary 灰才可见)
        if (i < 4) {
            lv_obj_t *sep = lv_obj_create(list);
            if (sep) {
                lv_obj_set_pos(sep, SEP_INSET, y + 38);
                lv_obj_set_size(sep, CARD_W - SEP_INSET * 2, 1);
                lv_obj_set_style_bg_color(sep, lv_color_hex(c->text_secondary), 0);
                lv_obj_set_style_bg_opa(sep, LV_OPA_30, 0);
                lv_obj_set_style_radius(sep, 0, 0);
                lv_obj_set_style_border_width(sep, 0, 0);
                lv_obj_set_style_pad_all(sep, 0, 0);
                lv_obj_remove_flag(sep, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象, 翻页时跳过)
// ---------------------------------------------------------------------------
void style_apple_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 已创建过则跳过 (翻页不再重建)
    if (s_card[0] || s_card[1]) return;

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // ── 标题栏 (持久): iOS 状态栏布局 — 中时间 + 右电池, 发丝线下分隔 ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, c->text_secondary);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM_MID, c->text_primary);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (图标字形只在 Montserrat 内, 本行无中文, 用 SYMBOL 字体)
    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, c->text_secondary);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (持久): 发丝线上分隔 ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_color(footer, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);

    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, c->text_secondary);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    // 页码用 systemBlue: 强调色只出现在活动位置 (进度条/页码)
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

    // ── 创建两页容器 (翻页只切换可见性) ──
    build_page0();
    build_page1();

    // 隐藏非当前页 (直接置位, 不播动画: 首次进入不折腾眼睛)
    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == s_page) {
            lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(s_card[i], LV_OPA_COVER, 0);
        } else {
            lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------------------
// 更新 (每秒调用)
// ---------------------------------------------------------------------------
void style_apple_update(void) {
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

    // 更新电池 (图标 + 颜色随电量分档, 由实时数据驱动)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    // 更新 page0 (仅当前页可见时)
    if (s_page == 0) {
        // 大数字进度 + 圆角条
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        if (s_bar) lv_bar_set_value(s_bar, st->mc_percent, LV_ANIM_ON);

        // 数据行 (行序与 build_page0 一致: order 中跳过 PERCENT)
        int row = 0;
        for (int i = 0; i < s_order_len && row < 6; i++) {
            int cmp = s_order[i];
            if (cmp == CMP_PERCENT) continue;
            if (!s_row_val[row]) { row++; continue; }
            lv_obj_t *val = s_row_val[row];

            switch (cmp) {
                case CMP_LAYER:
                    snprintf(buf, sizeof(buf), "%d/%d", st->layer_num, st->total_layer);
                    lv_label_set_text(val, buf); break;
                case CMP_NOZZLE:
                    snprintf(buf, sizeof(buf), "%d/%d°C",
                             (int)st->nozzle_temp, (int)st->nozzle_target);
                    lv_label_set_text(val, buf); break;
                case CMP_BED:
                    snprintf(buf, sizeof(buf), "%d/%d°C",
                             (int)st->bed_temp, (int)st->bed_target);
                    lv_label_set_text(val, buf); break;
                case CMP_CHAMBER:
                    snprintf(buf, sizeof(buf), "%d°C", (int)st->chamber_temp);
                    lv_label_set_text(val, buf); break;
                case CMP_REMAIN:
                    if (st->mc_remaining > 0) {
                        int h = st->mc_remaining / 60, m = st->mc_remaining % 60;
                        if (h > 0) snprintf(buf, sizeof(buf), "%d" L_HOUR "%02d" L_MIN, h, m);
                        else       snprintf(buf, sizeof(buf), "%d" L_MIN, m);
                    } else snprintf(buf, sizeof(buf), "--");
                    lv_label_set_text(val, buf); break;
                case CMP_STATE: {
                    // MQTT 未连接时显示 Connecting, 否则显示打印状态 (图标随状态变化)
                    bool conn = bambu_mqtt_connected();
                    snprintf(buf, sizeof(buf), "%s",
                             conn ? state_text(st->state) : L_CONNECTING);
                    lv_label_set_text(val, buf);
                    // 状态色由实时数据驱动 (左标签图标随右值同步变色)
                    uint32_t sc = c->text_secondary;
                    if (!conn)                                   sc = c->error;
                    else if (st->state == BAMBU_STATE_PAUSE)     sc = c->warning;
                    else if (st->state == BAMBU_STATE_FAILED)    sc = c->error;
                    else if (st->state == BAMBU_STATE_RUNNING)   sc = c->success;
                    lv_obj_set_style_text_color(val, lv_color_hex(sc), 0);
                    if (s_row_lbl[row]) {
                        lv_obj_set_style_text_color(s_row_lbl[row], lv_color_hex(sc), 0);
                        snprintf(buf, sizeof(buf), "%s " L_STATE,
                                 conn ? ui_theme_state_icon(st->state, true) : LV_SYMBOL_WIFI);
                        lv_label_set_text(s_row_lbl[row], buf);
                    }
                    break;
                }
                case CMP_SPEED:
                    snprintf(buf, sizeof(buf), "%d %d%%", st->spd_lvl, st->spd_mag);
                    lv_label_set_text(val, buf); break;
            }
            row++;
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
            if (t->type[0]) {
                snprintf(buf, sizeof(buf), "%s %s", name, t->type);
                if (s_ams_val[i]) {
                    char vb[12];
                    snprintf(vb, sizeof(vb), "%d%%", (int)t->remain);
                    lv_label_set_text(s_ams_val[i], vb);
                }
                if (s_ams_bar[i]) lv_bar_set_value(s_ams_bar[i], t->remain, LV_ANIM_OFF);
            } else {
                snprintf(buf, sizeof(buf), "%s %s", name, L_EMPTY);
                if (s_ams_val[i]) lv_label_set_text(s_ams_val[i], "--");
                if (s_ams_bar[i]) lv_bar_set_value(s_ams_bar[i], 0, LV_ANIM_OFF);
            }
            lv_label_set_text(s_ams_lbl[i], buf);
            // 活动料槽黑字, 其余灰: 强调只给正在工作的槽位
            lv_obj_set_style_text_color(s_ams_lbl[i],
                lv_color_hex(t->active ? c->text_primary : c->text_secondary), 0);
            lv_obj_set_style_text_color(s_ams_val[i],
                lv_color_hex(t->active ? c->text_primary : c->text_secondary), 0);

            // 色块按 MQTT 实时数据渲染 (透明料画空心描边)
            if (s_ams_swatch[i])
                ui_theme_tray_swatch(s_ams_swatch[i], t);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换 + 入场动画, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_apple_page_count(void) { return s_total_pages; }
int style_apple_current_page(void) { return s_page; }

static void show_page(int page) {
    for (int i = 0; i < 2; i++) {
        if (!s_card[i]) continue;
        if (i == page) {
            lv_obj_clear_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
            anim_enter(s_card[i]);
        } else {
            lv_obj_add_flag(s_card[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_pg_lbl) {
        char pg[16];
        snprintf(pg, sizeof(pg), "%d/%d", page + 1, s_total_pages);
        lv_label_set_text(s_pg_lbl, pg);
    }
}

void style_apple_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_apple_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
