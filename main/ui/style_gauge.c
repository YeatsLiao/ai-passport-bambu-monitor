// main/ui/style_gauge.c —— 风格10: 图形仪表盘风 (参考 BambuHelper 视觉)
// 布局: Page0 = 顶部细进度条 + 6 个 270° 圆弧仪表环 (2×3 网格) + ETA/任务名
//       Page1 = AMS 耗材竖条电池 (填充高度=余量, 颜色=实时耗材色) + ETA/任务信息
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

static const char *TAG __attribute__((unused)) = "style_gauge";

// ── UI 对象 ──
static int s_page = 0;
static int s_total_pages = 2;

static lv_obj_t *s_card[2] = {NULL, NULL};

// Page 0: 细进度条 + 六仪表环 + ETA
typedef struct {
    lv_obj_t *arc;   // 圆弧环 (值 0-100)
    lv_obj_t *val;   // 中心主数值
    lv_obj_t *sub;   // 中心副数值 (target/总数)
} gauge_t;
static gauge_t    s_g[6];
static lv_obj_t *s_prog_fill = NULL;    // 顶部细进度条填充
static lv_obj_t *s_state_lbl = NULL;    // 状态文字 (右上)
static lv_obj_t *s_eta_lbl   = NULL;    // ETA 大字
static lv_obj_t *s_file_lbl  = NULL;    // 任务名 (单行截断)

// Page 1: AMS 竖条电池
static lv_obj_t *s_tube[5];       // 外框
static lv_obj_t *s_fill[5];       // 底部填充块 (颜色=耗材实时色)
static lv_obj_t *s_tube_pct[5];   // 余量百分比
static lv_obj_t *s_file2_lbl  = NULL;   // 任务名
static lv_obj_t *s_layer_lbl  = NULL;   // 层数行
static lv_obj_t *s_eta2_lbl   = NULL;   // ETA 大字
static lv_obj_t *s_cur_dot    = NULL;   // 当前耗材色点
static lv_obj_t *s_cur_lbl    = NULL;   // 当前耗材文字

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

static lv_obj_t *mk_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    lv_obj_t *b = lv_obj_create(parent);
    if (!b) return NULL;
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(b, 1, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
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

// ETA 文本: 当前时间 + 剩余分钟 -> "14:15"; 无剩余时间时 "--:--"
static void eta_text(char *buf, size_t len) {
    const bambu_state_t *st = &g_bambu_state;
    if (st->mc_remaining <= 0) {
        snprintf(buf, len, "--:--");
        return;
    }
    time_t eta = time(NULL) + (time_t)st->mc_remaining * 60;
    struct tm ti = {0};
    localtime_r(&eta, &ti);
    snprintf(buf, len, "%02d:%02d", ti.tm_hour, ti.tm_min);
}

// ---------------------------------------------------------------------------
// 仪表环: 270° 圆弧 (135° 起顺时针), 中心主/副数值, 环下文字标签
// ---------------------------------------------------------------------------
static void mk_gauge(lv_obj_t *parent, gauge_t *g, int col, int y,
                     uint32_t color, const char *cap_text) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    const int cx_off = (col - 1) * 69;        // 列中心相对父容器中线的偏移
    memset(g, 0, sizeof(*g));

    lv_obj_t *arc = lv_arc_create(parent);
    if (arc) {
        lv_obj_set_size(arc, 56, 56);
        // LV_ALIGN_TOP_MID 的 x 偏移即对象中心相对父中线的偏移
        lv_obj_align(arc, LV_ALIGN_TOP_MID, cx_off, y);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 0);
        lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   // 去中心旋钮
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_arc_width(arc, 4, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc, lv_color_hex(c->border), LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    }
    g->arc = arc;

    // 主/副两行拉开 2px 以上, 避免 montserrat 行高溢出导致重叠
    g->val = mk_lbl(arc, "--", L_FONT_NUM_MID, c->text_primary);
    if (g->val) lv_obj_align(g->val, LV_ALIGN_CENTER, 0, -10);
    g->sub = mk_lbl(arc, "", L_FONT_NUM, c->text_secondary);
    if (g->sub) lv_obj_align(g->sub, LV_ALIGN_CENTER, 0, 11);

    // 环下标签 (固定宽度 + 居中, 避免中文/英文宽度差导致偏移)
    lv_obj_t *cap = mk_lbl(parent, cap_text, L_FONT_TEXT, c->text_secondary);
    if (cap) {
        lv_obj_set_width(cap, 70);
        lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(cap, LV_ALIGN_TOP_MID, cx_off, y + 59);
    }
}

static void upd_gauge(gauge_t *g, int pct, const char *val, const char *sub) {
    if (!g->arc) return;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    lv_arc_set_value(g->arc, pct);
    if (g->val) lv_label_set_text(g->val, val);
    if (g->sub) lv_label_set_text(g->sub, sub);
}

// ---------------------------------------------------------------------------
// Page 0: 细进度条 + 六仪表环 + ETA/任务名
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[0] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);   // 统一卡片几何 (8,34) 224×252
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 顶部细进度条 + 状态文字 (右上, 对齐参考图) ──
    mk_block(card, 0, 0, 208, 3, c->border);
    s_prog_fill = mk_block(card, 0, 0, 1, 3, c->accent);
    s_state_lbl = mk_lbl(card, "-- " L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_align(s_state_lbl, LV_ALIGN_TOP_RIGHT, 0, 8);

    // ── 六个仪表环 (2 行 × 3 列) ──
    // 环色取主题语义变量: 进度/层数绿, 喷嘴橙, 热床/速度青, 腔体绿
    mk_gauge(card, &s_g[0], 0, 24, c->success,       L_PROGRESS);
    mk_gauge(card, &s_g[1], 1, 24, c->gauge_nozzle,  L_NOZZLE);
    mk_gauge(card, &s_g[2], 2, 24, c->gauge_bed,     L_BED);
    mk_gauge(card, &s_g[3], 0, 104, c->gauge_bed,    L_SPEED);
    mk_gauge(card, &s_g[4], 1, 104, c->success,      L_LAYER);
    mk_gauge(card, &s_g[5], 2, 104, c->gauge_chamber, L_CHAMBER);

    // ── 底部: ETA 大字 + 任务名 (NUM_BIG 实际行高≈31px, 与任务名留 5px 间隙) ──
    mk_block(card, 0, 182, 208, 1, c->border);
    s_eta_lbl = mk_lbl(card, "ETA --:--", L_FONT_NUM_BIG, c->accent);
    if (s_eta_lbl) lv_obj_align(s_eta_lbl, LV_ALIGN_TOP_LEFT, 0, 183);
    s_file_lbl = mk_lbl(card, L_EMPTY, L_FONT_TEXT, c->text_secondary);
    if (s_file_lbl) {
        // DOT 截断必须同时固定宽与高, 否则回退为 WRAP 变两行挤压下方元素
        lv_label_set_long_mode(s_file_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_size(s_file_lbl, 208, 17);
        lv_obj_align(s_file_lbl, LV_ALIGN_TOP_LEFT, 0, 219);
    }
}

// ---------------------------------------------------------------------------
// Page 1: AMS 竖条电池 + ETA/任务信息
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *card = lv_obj_create(s_content_area);
    if (!card) return;
    s_card[1] = card;

    lv_obj_set_pos(card, 8, 34);
    lv_obj_set_size(card, 224, 252);
    lv_obj_set_style_bg_color(card, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(card, c->radius, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // ── 顶部: 任务名 + 层数 + ETA ──
    s_file2_lbl = mk_lbl(card, L_EMPTY, L_FONT_TEXT, c->text_secondary);
    if (s_file2_lbl) {
        lv_label_set_long_mode(s_file2_lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_size(s_file2_lbl, 208, 18);
        lv_obj_align(s_file2_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    }
    s_layer_lbl = mk_lbl(card, L_LAYER " --/--", L_FONT_TEXT, c->text_secondary);
    if (s_layer_lbl) lv_obj_align(s_layer_lbl, LV_ALIGN_TOP_LEFT, 0, 22);
    s_eta2_lbl = mk_lbl(card, "ETA --:--", L_FONT_NUM_BIG, c->accent);
    if (s_eta2_lbl) lv_obj_align(s_eta2_lbl, LV_ALIGN_TOP_LEFT, 0, 44);

    // ── AMS 竖条电池 (4 槽 + 外置): 填充高度=余量, 颜色=实时耗材色 ──
    memset(s_tube, 0, sizeof(s_tube));
    memset(s_fill, 0, sizeof(s_fill));
    memset(s_tube_pct, 0, sizeof(s_tube_pct));

    mk_block(card, 0, 84, 208, 1, c->border);
    for (int i = 0; i < 5; i++) {
        int x = 18 + i * 38;   // 5 条均分 208 宽 (条宽 20, 间距 18)
        lv_obj_t *tube = lv_obj_create(card);
        if (tube) {
            lv_obj_set_pos(tube, x, 96);
            lv_obj_set_size(tube, 20, 64);
            lv_obj_remove_flag(tube, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            // 必须清零 lv_obj 默认内边距, 否则填充块被推移+裁剪 (16px 宽只剩半截)
            lv_obj_set_style_pad_all(tube, 0, 0);
            lv_obj_set_style_radius(tube, 4, 0);
            lv_obj_set_style_border_width(tube, 1, 0);
            lv_obj_set_style_border_color(tube, lv_color_hex(c->border), 0);
            lv_obj_set_style_bg_color(tube, lv_color_hex(c->card_bg), 0);
        }
        s_tube[i] = tube;

        // 内部填充块 (底部对齐, 高度随余量伸缩; 内腔 16×60, 边距 2)
        lv_obj_t *fill = lv_obj_create(tube);
        if (fill) {
            lv_obj_set_pos(fill, 2, 62);
            lv_obj_set_size(fill, 16, 0);
            lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(fill, 3, 0);
            lv_obj_set_style_border_width(fill, 0, 0);
            lv_obj_set_style_bg_color(fill, lv_color_hex(c->text_secondary), 0);
            lv_obj_set_style_bg_opa(fill, LV_OPA_30, 0);
        }
        s_fill[i] = fill;

        // 条下标签: 槽号 / 余量百分比
        char buf[12];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d", i + 1);
        else       snprintf(buf, sizeof(buf), "%s", L_EXT);
        lv_obj_t *lbl = mk_lbl(card, buf, L_FONT_NUM, c->text_secondary);
        if (lbl) {
            lv_obj_set_width(lbl, 48);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_MID, x - 94, 164);   // 中心 = x+10
        }
        s_tube_pct[i] = mk_lbl(card, "--", L_FONT_NUM, c->text_secondary);
        if (s_tube_pct[i]) {
            lv_obj_set_width(s_tube_pct[i], 48);
            lv_obj_set_style_text_align(s_tube_pct[i], LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(s_tube_pct[i], LV_ALIGN_TOP_MID, x - 94, 180);
        }
    }

    // ── 底部: 当前耗材 (色点 + 类型/余量) ──
    s_cur_dot = mk_block(card, 0, 204, 12, 12, c->text_secondary);
    if (s_cur_dot) lv_obj_set_style_radius(s_cur_dot, 2, 0);
    s_cur_lbl = mk_lbl(card, L_EMPTY, L_FONT_TEXT, c->text_primary);
    if (s_cur_lbl) lv_obj_align(s_cur_lbl, LV_ALIGN_TOP_LEFT, 18, 204);
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_gauge_build(void) {
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

    s_bat_lbl = mk_lbl(header, LV_SYMBOL_BATTERY_FULL " --", L_FONT_SYMBOL, c->accent);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -4, 0);

    // ── 底部栏 (持久) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
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
void style_gauge_update(void) {
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();
    char buf[48], buf2[24];

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

    // 电池 (图标与颜色随电量分档)
    if (s_bat_lbl) {
        int soc = bsp_battery_soc();
        if (soc >= 0) snprintf(buf, sizeof(buf), "%s %d%%", ui_theme_battery_icon(soc), soc);
        else          snprintf(buf, sizeof(buf), "%s --", ui_theme_battery_icon(-1));
        lv_label_set_text(s_bat_lbl, buf);
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    // 状态行 (图标随状态变化, 颜色语义化)
    if (s_state_lbl) {
        bool conn = bambu_mqtt_connected();
        uint32_t sc = conn ? state_color(st->state, c) : c->error;
        if (conn) snprintf(buf, sizeof(buf), "%s %s",
                           ui_theme_state_icon(st->state, true), state_text(st->state));
        else      snprintf(buf, sizeof(buf), "%s " L_CONNECTING, LV_SYMBOL_WIFI);
        lv_label_set_text(s_state_lbl, buf);
        lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(sc), 0);
    }

    // ETA (两页共用逻辑)
    eta_text(buf2, sizeof(buf2));
    if (s_eta_lbl) {
        snprintf(buf, sizeof(buf), "ETA %s", buf2);
        lv_label_set_text(s_eta_lbl, buf);
    }
    if (s_eta2_lbl) {
        snprintf(buf, sizeof(buf), "ETA %s", buf2);
        lv_label_set_text(s_eta2_lbl, buf);
    }

    // 任务名 (两页)
    if (st->gcode_file[0]) {
        if (s_file_lbl)  lv_label_set_text(s_file_lbl, st->gcode_file);
        if (s_file2_lbl) lv_label_set_text(s_file2_lbl, st->gcode_file);
    }

    // ── Page 0: 六仪表环 ──
    if (s_page == 0) {
        // 细进度条
        if (s_prog_fill) {
            int w = 208 * st->mc_percent / 100;
            if (st->mc_percent > 0 && w < 2) w = 2;
            lv_obj_set_width(s_prog_fill, w);
        }
        // 环1 进度: 值=mc_percent, 中心=纯数字, 副行=剩余时间/百分号
        if (st->mc_remaining > 0)
            snprintf(buf2, sizeof(buf2), "%dh%02dm", st->mc_remaining / 60, st->mc_remaining % 60);
        else
            snprintf(buf2, sizeof(buf2), "%%");
        snprintf(buf, sizeof(buf), "%d", st->mc_percent);
        upd_gauge(&s_g[0], st->mc_percent, buf, buf2);

        // 环2 喷嘴: 值=当前/目标, 中心=当前温度, 副行=/目标 (无目标时显示单位)
        int nz = (st->nozzle_target > 1) ? (int)(100 * st->nozzle_temp / st->nozzle_target) : 0;
        snprintf(buf, sizeof(buf), "%d", (int)st->nozzle_temp);
        if (st->nozzle_target > 0) snprintf(buf2, sizeof(buf2), "/%d", (int)st->nozzle_target);
        else                       snprintf(buf2, sizeof(buf2), "°C");
        upd_gauge(&s_g[1], nz, buf, buf2);

        // 环3 热床: 同喷嘴
        int bd = (st->bed_target > 1) ? (int)(100 * st->bed_temp / st->bed_target) : 0;
        snprintf(buf, sizeof(buf), "%d", (int)st->bed_temp);
        if (st->bed_target > 0) snprintf(buf2, sizeof(buf2), "/%d", (int)st->bed_target);
        else                    snprintf(buf2, sizeof(buf2), "°C");
        upd_gauge(&s_g[2], bd, buf, buf2);

        // 环4 速度: 值=spd_mag, 中心=等级, 副行=百分比
        snprintf(buf, sizeof(buf), "L%d", st->spd_lvl);
        snprintf(buf2, sizeof(buf2), "%d%%", st->spd_mag);
        upd_gauge(&s_g[3], st->spd_mag, buf, buf2);

        // 环5 层数: 值=层/总层, 中心=当前层, 副行=/总层
        int ly = (st->total_layer > 0) ? (100 * st->layer_num / st->total_layer) : 0;
        snprintf(buf, sizeof(buf), "%d", st->layer_num);
        snprintf(buf2, sizeof(buf2), "/%d", st->total_layer);
        upd_gauge(&s_g[4], ly, buf, buf2);

        // 环6 腔体: 值按 0-60°C 映射, 中心=温度, 副行=°C
        int ch = (int)(100 * st->chamber_temp / 60);
        snprintf(buf, sizeof(buf), "%d", (int)st->chamber_temp);
        upd_gauge(&s_g[5], ch, buf, "°C");
    }

    // ── Page 1: AMS 竖条电池 ──
    if (s_page == 1) {
        // 层数行
        if (s_layer_lbl) {
            snprintf(buf, sizeof(buf), L_LAYER " %d/%d", st->layer_num, st->total_layer);
            lv_label_set_text(s_layer_lbl, buf);
        }
        for (int i = 0; i < 5; i++) {
            if (!s_fill[i]) continue;
            bool no_tray = (i < 4 && i >= st->ams_count);
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;
            bool empty = no_tray || !t->type[0];

            // 填充: 高度=余量, 颜色=耗材实时色 (透明料半透明灰)
            int fh = empty ? 0 : (60 * (int)t->remain / 100);
            if (fh < 0) fh = 0;
            if (fh > 60) fh = 60;
            if (!empty && fh < 6) fh = 6;   // 有料但余量极低时留可见残条
            lv_obj_set_height(s_fill[i], fh);
            lv_obj_set_y(s_fill[i], 62 - fh);
            if (t->translucent && !empty) {
                lv_obj_set_style_bg_color(s_fill[i], lv_color_hex(c->text_secondary), 0);
                lv_obj_set_style_bg_opa(s_fill[i], LV_OPA_30, 0);
            } else if (!empty) {
                lv_obj_set_style_bg_color(s_fill[i], ui_theme_hex_color(t->color), 0);
                lv_obj_set_style_bg_opa(s_fill[i], LV_OPA_COVER, 0);
            }

            // 当前料槽高亮边框
            bool active = (i < 4) ? t->active : (st->active_tray >= 4);
            if (s_tube[i]) {
                lv_obj_set_style_border_width(s_tube[i], active ? 2 : 1, 0);
                lv_obj_set_style_border_color(s_tube[i],
                    lv_color_hex(active ? c->accent : c->border), 0);
            }

            // 百分比
            if (s_tube_pct[i]) {
                if (empty) lv_label_set_text(s_tube_pct[i], "--");
                else {
                    snprintf(buf, sizeof(buf), "%d%%", (int)t->remain);
                    lv_label_set_text(s_tube_pct[i], buf);
                }
            }
        }

        // 当前耗材 (色点 + 类型/余量)
        const bambu_ams_tray_t *cur = NULL;
        for (int i = 0; i < 4 && !cur; i++)
            if (st->trays[i].active && st->trays[i].type[0]) cur = &st->trays[i];
        if (!cur && st->active_tray >= 4 && st->vt_tray.type[0]) cur = &st->vt_tray;
        if (s_cur_lbl) {
            if (cur) snprintf(buf, sizeof(buf), "%s %d%%", cur->type, (int)cur->remain);
            else     snprintf(buf, sizeof(buf), L_EMPTY);
            lv_label_set_text(s_cur_lbl, buf);
        }
        if (s_cur_dot) {
            if (cur) {
                lv_obj_set_style_bg_color(s_cur_dot, ui_theme_hex_color(cur->color), 0);
                lv_obj_set_style_bg_opa(s_cur_dot, cur->translucent ? LV_OPA_30 : LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_color(s_cur_dot, lv_color_hex(c->text_secondary), 0);
                lv_obj_set_style_bg_opa(s_cur_dot, LV_OPA_30, 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_gauge_page_count(void) { return s_total_pages; }
int style_gauge_current_page(void) { return s_page; }

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

void style_gauge_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_gauge_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
