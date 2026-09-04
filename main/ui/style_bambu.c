// main/ui/style_bambu.c —— 风格1: 拓竹原厂工业风
// 布局: Page0 = 圆环进度仪表 (lv_arc) + 图标状态行 + 2x2 数据格子 (内容由 CFG_COMPONENT_ORDER 决定)
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

// 组件顺序: config.h 定义 CFG_COMPONENT_ORDER (如 {5,4,1,2,6,8}) 则覆盖默认值。
// 两个分支都要能解析出 s_order, 否则用户一旦定义该宏就报 undeclared (实测踩过)。
#ifdef CFG_COMPONENT_ORDER
static const int s_order[] = CFG_COMPONENT_ORDER;
#else
static const int s_default_order[] = {5, 4, 1, 2, 6, 8};
#define s_order s_default_order
#endif
#define s_order_len (sizeof(s_order) / sizeof(s_order[0]))

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 1 + HAS_AMS;

// 两页卡片容器 (翻页时只切换可见性)
static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0: 圆环仪表 + 状态行 + 2x2 数据格子
static lv_obj_t *s_arc        = NULL;   // 进度圆环
static lv_obj_t *s_pct_lbl    = NULL;   // 圆环中央大字百分比
static lv_obj_t *s_state_icon = NULL;   // 状态图标 (播放/暂停/警告...)
static lv_obj_t *s_state_lbl  = NULL;   // 状态文字
static int       s_cell_cmp[4];         // 2x2 格子各格展示的组件编号
static lv_obj_t *s_cell_lbl[4];         // 格子的 图标+名称 行
static lv_obj_t *s_cell_val[4];         // 格子的 数值 行

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

// 透明行容器: flex 横向居中排列图标+文字, 中英文宽度变化不会破居中
static lv_obj_t *mk_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    if (!row) return NULL;
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

// 组件编号 -> 本地化名称 (格子第一行用)
static const char *cmp_name(int cmp) {
    switch (cmp) {
        case CMP_NOZZLE:  return L_NOZZLE;
        case CMP_BED:     return L_BED;
        case CMP_CHAMBER: return L_CHAMBER;
        case CMP_LAYER:   return L_LAYER;
        case CMP_REMAIN:  return L_REMAIN;
        case CMP_SPEED:   return L_SPEED;
        case CMP_PERCENT: return "%";
        default:          return L_STATE;
    }
}

// 组件编号 -> 数值行颜色 (温度类用专用仪表色)
static uint32_t cmp_color(int cmp, const ui_theme_colors_t *c) {
    switch (cmp) {
        case CMP_NOZZLE:  return c->gauge_nozzle;
        case CMP_BED:     return c->gauge_bed;
        case CMP_CHAMBER: return c->gauge_chamber;
        case CMP_SPEED:   return c->accent;
        default:          return c->text_primary;
    }
}

// 组件编号 -> 当前数值文本 (纯数字/ASCII, 因此可用 Montserrat 大字号)
static void cmp_value(char *buf, size_t n, int cmp, const bambu_state_t *st) {
    switch (cmp) {
        case CMP_NOZZLE:
            snprintf(buf, n, "%d/%d°C", (int)st->nozzle_temp, (int)st->nozzle_target); break;
        case CMP_BED:
            snprintf(buf, n, "%d/%d°C", (int)st->bed_temp, (int)st->bed_target); break;
        case CMP_CHAMBER:
            snprintf(buf, n, "%d°C", (int)st->chamber_temp); break;
        case CMP_LAYER:
            snprintf(buf, n, "%d/%d", st->layer_num, st->total_layer); break;
        case CMP_SPEED:
            snprintf(buf, n, "%d%%", st->spd_mag); break;
        case CMP_REMAIN:
            if (st->mc_remaining > 0)
                snprintf(buf, n, "%dh%02dm", st->mc_remaining / 60, st->mc_remaining % 60);
            else
                snprintf(buf, n, "--");
            break;
        case CMP_PERCENT:
            snprintf(buf, n, "%d%%", st->mc_percent); break;
        default:
            snprintf(buf, n, "--"); break;
    }
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
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 消除底部背景空带
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius + 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 圆环进度仪表 (270° 弧, 顶部居中) ──
    lv_obj_t *arc = lv_arc_create(card);
    if (arc) {
        lv_obj_set_pos(arc, 58, 0);
        lv_obj_set_size(arc, 92, 92);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);          // 隐藏旋钮
        lv_obj_set_style_arc_width(arc, 12, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 12, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(c->border), LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(c->accent), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    }
    s_arc = arc;

    // 中央大字百分比 (纯数字, 两种语言都用 Montserrat 28)
    s_pct_lbl = mk_lbl(card, "--%", L_FONT_NUM_BIG, c->text_primary);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_MID, 0, 29);

    // ── 状态行: 图标 + 文字 (flex 居中, 中英文宽度变化不破居中) ──
    lv_obj_t *srow = mk_row(card);
    if (srow) {
        lv_obj_align(srow, LV_ALIGN_TOP_MID, 0, 96);
        s_state_icon = mk_lbl(srow, LV_SYMBOL_WIFI, L_FONT_SYMBOL, c->text_secondary);
        s_state_lbl  = mk_lbl(srow, L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    }

    // ── 2x2 数据格子 (内容由 CFG_COMPONENT_ORDER 决定) ──
    // 百分比/状态/AMS 已有专属位置, 剩下的数值型组件取前 4 个
    static const int cell_default[4] = {CMP_NOZZLE, CMP_BED, CMP_LAYER, CMP_REMAIN};
    int n = 0;
    for (int i = 0; i < (int)s_order_len && n < 4; i++) {
        int cmp = s_order[i];
        if (cmp == CMP_PERCENT || cmp == CMP_STATE || cmp == CMP_AMS) continue;
        s_cell_cmp[n++] = cmp;
    }
    for (int i = n; i < 4; i++) s_cell_cmp[i] = cell_default[i];

    for (int i = 0; i < 4; i++) {
        int cmp = s_cell_cmp[i];
        int x = (i % 2) * 104;
        int y = 128 + (i / 2) * 50;   // 卡片加高后网格重排, 底部不留大段空白
        char buf[48];

        // 第一行: 图标 + 本地化名称
        snprintf(buf, sizeof(buf), "%s %s", ICO(cmp), cmp_name(cmp));
        s_cell_lbl[i] = mk_lbl(card, buf, L_FONT_TEXT, c->text_secondary);
        if (s_cell_lbl[i]) lv_obj_set_pos(s_cell_lbl[i], x, y);

        // 第二行: 数值 (纯数字, 用 Montserrat 20 突出)
        s_cell_val[i] = mk_lbl(card, "--", L_FONT_NUM_MID, cmp_color(cmp, c));
        if (s_cell_val[i]) lv_obj_set_pos(s_cell_val[i], x, y + 18);
    }
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
    lv_obj_set_size(card, 224, 252);   // 卡片下探到 footer 上沿 (y286), 消除底部背景空带
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius + 2, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    mk_lbl(card, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->accent);

    // 格子初始背景 = 主题边框色, 文字取其对比色 (有料时 update 会按耗材颜色重算)
    const uint32_t slot_txt = ui_theme_on_color(c->border);

    // ── 4 个 AMS 料槽格子 (2x2) ──
    const int sx[4] = {0, 104, 0, 104};
    const int sy[4] = {32, 32, 102, 102};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *box = mk_slot(card, sx[i], sy[i], 100, 62);
        s_ams_box[i] = box;
        s_ams_type[i] = NULL;
        s_ams_rem[i] = NULL;
        if (!box) continue;
        // 槽号 (左上角小字, 格子内相对定位)
        lv_obj_t *num = mk_lbl(box, "", L_FONT_TEXT, slot_txt);
        if (num) {
            lv_obj_align(num, LV_ALIGN_TOP_LEFT, 4, 2);
            char n[8];
            snprintf(n, sizeof(n), "#%d", i + 1);
            lv_label_set_text(num, n);
        }
        // 类型 (居中大字) + 余量 (下方小字)
        s_ams_type[i] = mk_lbl(box, L_EMPTY, L_FONT_TEXT, slot_txt);
        if (s_ams_type[i]) lv_obj_align(s_ams_type[i], LV_ALIGN_CENTER, 0, -6);
        s_ams_rem[i] = mk_lbl(box, "--", L_FONT_NUM, slot_txt);
        if (s_ams_rem[i]) lv_obj_align(s_ams_rem[i], LV_ALIGN_CENTER, 0, 12);
    }

    // ── Ext 外挂料槽 (底部宽格子) ──
    lv_obj_t *box = mk_slot(card, 0, 174, 204, 48);   // 旧布局 y166+h44=210 超出内容区被裁 8px
    s_ams_box[4] = box;
    s_ams_type[4] = NULL;
    s_ams_rem[4] = NULL;
    if (box) {
        lv_obj_t *num = mk_lbl(box, L_EXT, L_FONT_TEXT, slot_txt);
        if (num) lv_obj_align(num, LV_ALIGN_LEFT_MID, 6, 0);
        s_ams_type[4] = mk_lbl(box, L_EMPTY, L_FONT_TEXT, slot_txt);
        if (s_ams_type[4]) lv_obj_align(s_ams_type[4], LV_ALIGN_LEFT_MID, 40, 0);
        s_ams_rem[4] = mk_lbl(box, "--", L_FONT_NUM, slot_txt);
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

    // 标题栏/底部栏文字颜色按各自背景亮度自动取黑/白, 不硬编码
    const uint32_t htxt = ui_theme_on_color(c->header_bg);
    const uint32_t ftxt = ui_theme_on_color(c->footer_bg);

    lv_obj_t *title = mk_lbl(header, L_TITLE_BAMBU, L_FONT_TEXT, htxt);
    if (title) lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, htxt);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_CENTER, 0, 0);

    // 电池图标 + 百分比 (本行无中文, 用 SYMBOL 字体)
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

    // ── 创建两页卡片 (都创建, 翻页只切换可见性) ──
    memset(s_card, 0, sizeof(s_card));
    memset(s_ams_box, 0, sizeof(s_ams_box));
    memset(s_ams_type, 0, sizeof(s_ams_type));
    memset(s_ams_rem, 0, sizeof(s_ams_rem));
    memset(s_cell_lbl, 0, sizeof(s_cell_lbl));
    memset(s_cell_val, 0, sizeof(s_cell_val));
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

    // 电池 (图标随电量分档)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        // 图标颜色随电量分档 (满电绿 / 中低黄 / 低电红), 由实时数据驱动
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    // ── Page 0: 仪表 + 格子 ──
    if (s_page == 0) {
        if (s_arc) lv_arc_set_value(s_arc, st->mc_percent);
        if (s_pct_lbl) {
            snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
            lv_label_set_text(s_pct_lbl, buf);
        }
        // 状态: 图标与文字同色 (未连接时统一显示 WiFi 图标 + error 色)
        if (s_state_lbl) {
            bool conn = bambu_mqtt_connected();
            uint32_t sc = conn ? state_color(st->state, c) : c->error;
            lv_label_set_text(s_state_lbl, conn ? state_text(st->state) : L_CONNECTING);
            lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(sc), 0);
            if (s_state_icon) {
                lv_label_set_text(s_state_icon,
                    conn ? ui_theme_state_icon(st->state, true) : LV_SYMBOL_WIFI);
                lv_obj_set_style_text_color(s_state_icon, lv_color_hex(sc), 0);
            }
        }
        // 2x2 格子数值 (格子展示的组件由 CFG_COMPONENT_ORDER 决定)
        for (int i = 0; i < 4; i++) {
            if (!s_cell_val[i]) continue;
            cmp_value(buf, sizeof(buf), s_cell_cmp[i], st);
            lv_label_set_text(s_cell_val[i], buf);
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

            // 有料: 背景 = 耗材颜色 (透明料画空心), 文字按亮度自动黑白
            if (t->translucent) {
                lv_obj_set_style_bg_color(s_ams_box[i], lv_color_hex(c->border), 0);
                lv_obj_set_style_text_color(s_ams_type[i],
                    lv_color_hex(c->text_primary), 0);
                if (s_ams_rem[i])
                    lv_obj_set_style_text_color(s_ams_rem[i],
                        lv_color_hex(c->text_secondary), 0);
            } else {
                lv_obj_set_style_bg_color(s_ams_box[i], lv_color_hex(rgb), 0);
                lv_color_t tc = ui_theme_contrast_text(rgb);
                lv_obj_set_style_text_color(s_ams_type[i], tc, 0);
                if (s_ams_rem[i]) lv_obj_set_style_text_color(s_ams_rem[i], tc, 0);
            }
            lv_label_set_text(s_ams_type[i], t->type);
            if (s_ams_rem[i]) {
                snprintf(buf, sizeof(buf), "%d%%", (int)t->remain);
                lv_label_set_text(s_ams_rem[i], buf);
            }
        }

        // 当前使用中的料槽: 强调色高亮边框
        for (int i = 0; i < 5; i++) {
            if (!s_ams_box[i]) continue;
            bool active = (i < 4) ? st->trays[i].active : (st->active_tray >= 4);
            lv_obj_set_style_border_color(s_ams_box[i],
                lv_color_hex(active ? c->accent : c->card_bg), 0);
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
