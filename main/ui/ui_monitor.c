// main/ui/ui_monitor.c —— 监控页面（4 种风格布局）
//
// 根据 config.h 的 CFG_UI_STYLE 编译不同的布局:
//   STYLE_COMPACT   简洁文本 — 全部信息一屏显示
//   STYLE_DASHBOARD 仪表盘  — 弧线温度表 + 进度环
//   STYLE_CARD      卡片    — 分页浏览（UP/DOWN 翻页）
//   STYLE_CUTE      可爱    — 圆角 + 装饰 + 马卡龙色
//
// 按键交互:
//   UP/DOWN 短按: 翻页（CARD/CUTE 风格）或无操作（COMPACT/DASHBOARD）
//   OK 短按:      刷新数据（pushall）
//   OK 长按:      返回

#include "ui_monitor.h"
#include "ui_theme.h"
#include "../bambu_state.h"
#include "../bambu_mqtt.h"
#include "../config.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui_monitor";

static lv_obj_t *s_scr = NULL;
static lv_timer_t *s_refresh_timer = NULL;

// 卡片风格的分页
static int s_card_page = 0;
#define CARD_PAGE_COUNT 3  // 0=主状态, 1=温度详情, 2=AMS

// UI 组件指针（直接保存，不用 user_data 查找）
static struct {
    lv_obj_t *nozzle, *bed, *chamber;
    lv_obj_t *layer, *percent, *remain;
    lv_obj_t *state, *speed, *status;
    lv_obj_t *bar;  // progress bar bg (bar is child of bg)
    lv_obj_t *fan1, *fan2;  // 风扇标签（页面1）
} ui;

// 分页相关
static lv_obj_t *s_content_area = NULL;
static lv_obj_t *s_page_indicator = NULL;

// 前向声明
static void rebuild_page(void);
static void update_page_indicator(void);

// ---------------------------------------------------------------------------
// 通用: 创建带主题样式的标签
// ---------------------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, uint32_t color) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

static lv_obj_t *make_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    if (ui_theme_is_cute()) {
        lv_obj_set_style_radius(panel, 12, 0);
    } else {
        lv_obj_set_style_radius(panel, 6, 0);
    }
    lv_obj_set_style_pad_all(panel, 8, 0);
    return panel;
}

// ---------------------------------------------------------------------------
// 更新函数: 从 g_bambu_state 读取并刷新 UI
// ---------------------------------------------------------------------------
static void update_ui(void) {
    if (!s_scr) return;
    bambu_state_t *st = &g_bambu_state;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    char buf[64];

    if (ui.nozzle) {
        snprintf(buf, sizeof(buf), "Nozzle %d/%d°C",
                 (int)st->nozzle_temp, (int)st->nozzle_target);
        lv_label_set_text(ui.nozzle, buf);
        lv_obj_set_style_text_color(ui.nozzle, lv_color_hex(c->gauge_nozzle), 0);
    }
    if (ui.bed) {
        snprintf(buf, sizeof(buf), "Bed %d/%d°C",
                 (int)st->bed_temp, (int)st->bed_target);
        lv_label_set_text(ui.bed, buf);
        lv_obj_set_style_text_color(ui.bed, lv_color_hex(c->gauge_bed), 0);
    }
    if (ui.chamber) {
        snprintf(buf, sizeof(buf), "Chamber %d°C", (int)st->chamber_temp);
        lv_label_set_text(ui.chamber, buf);
    }
    if (ui.layer) {
        snprintf(buf, sizeof(buf), "Layer %d/%d", st->layer_num, st->total_layer);
        lv_label_set_text(ui.layer, buf);
    }
    if (ui.percent) {
        snprintf(buf, sizeof(buf), "%d%%", st->mc_percent);
        lv_label_set_text(ui.percent, buf);
    }
    if (ui.remain) {
        if (st->mc_remaining > 0) {
            int h = st->mc_remaining / 60;
            int m = st->mc_remaining % 60;
            snprintf(buf, sizeof(buf), "Remain %dh%dm", h, m);
        } else {
            snprintf(buf, sizeof(buf), "Remain --");
        }
        lv_label_set_text(ui.remain, buf);
    }
    if (ui.state) {
        const char *state_text = bambu_state_str(st->state);
        lv_label_set_text(ui.state, state_text);
        uint32_t state_color = c->success;
        if (st->state == BAMBU_STATE_PAUSE) state_color = c->warning;
        else if (st->state == BAMBU_STATE_FAILED) state_color = c->error;
        else if (st->state == BAMBU_STATE_IDLE) state_color = c->text_secondary;
        lv_obj_set_style_text_color(ui.state, lv_color_hex(state_color), 0);
    }
    if (ui.speed) {
        snprintf(buf, sizeof(buf), "Speed %d %d%%", st->spd_lvl, st->spd_mag);
        lv_label_set_text(ui.speed, buf);
    }
    if (ui.status) {
        lv_label_set_text(ui.status, bambu_mqtt_status_str());
    }

    // 进度条
    if (ui.bar) {
        lv_bar_set_value(ui.bar, st->mc_percent, LV_ANIM_ON);
    }

    // 风扇（页面1）
    if (ui.fan1) {
        snprintf(buf, sizeof(buf), "Cooling %d%%", st->cooling_fan);
        lv_label_set_text(ui.fan1, buf);
    }
    if (ui.fan2) {
        snprintf(buf, sizeof(buf), "Part Fan %d%%", st->big_fan1);
        lv_label_set_text(ui.fan2, buf);
    }
}

// ---------------------------------------------------------------------------
// 定时器回调
// ---------------------------------------------------------------------------
static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    update_ui();
}

// ---------------------------------------------------------------------------
// 构建 UI（根据风格不同）
// ---------------------------------------------------------------------------
#if CFG_UI_STYLE == STYLE_COMPACT
// ===== 简洁文本风格 =====
static void build_compact_ui(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 背景
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // 标题栏
    lv_obj_t *title = make_label(s_scr, "Bambu X1-Carbon",
                                  &lv_font_montserrat_14, c->accent);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 8);

    // 连接状态
    ui.status = make_label(s_scr, "...",
                                   &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(ui.status, LV_ALIGN_TOP_RIGHT, -8, 8);

    // 主内容区
    int y = 36;
    int line_h = 28;

    ui.nozzle = make_label(s_scr, "Nozzle --/--°C",
                                    &lv_font_montserrat_14, c->gauge_nozzle);
    lv_obj_set_pos(ui.nozzle, 8, y); y += line_h;

    ui.bed = make_label(s_scr, "Bed --/--°C",
                                 &lv_font_montserrat_14, c->gauge_bed);
    lv_obj_set_pos(ui.bed, 8, y); y += line_h;

    ui.chamber = make_label(s_scr, "Chamber --°C",
                                    &lv_font_montserrat_14, c->gauge_chamber);
    lv_obj_set_pos(ui.chamber, 8, y); y += line_h;

    y += 4;
    ui.layer = make_label(s_scr, "Layer --/--",
                                  &lv_font_montserrat_20, c->text_primary);
    lv_obj_set_pos(ui.layer, 8, y); y += 32;

    // 进度条
    lv_obj_t *bar_bg = lv_obj_create(s_scr);
    lv_obj_set_pos(bar_bg, 8, y);
    lv_obj_set_size(bar_bg, 224, 20);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);
    lv_obj_set_style_radius(bar_bg, 4, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);

    lv_obj_t *bar = lv_bar_create(bar_bg);
    lv_obj_set_size(bar, 220, 16);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), 0);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    ui.bar = bar;
    y += 28;

    ui.percent = make_label(s_scr, "--%",
                                    &lv_font_montserrat_20, c->accent);
    lv_obj_align(ui.percent, LV_ALIGN_CENTER, 0, y - 10);
    y += 28;

    ui.remain = make_label(s_scr, "Remain --",
                                   &lv_font_montserrat_14, c->text_secondary);
    lv_obj_set_pos(ui.remain, 8, y); y += line_h;

    ui.state = make_label(s_scr, "IDLE",
                                  &lv_font_montserrat_14, c->text_secondary);
    lv_obj_set_pos(ui.state, 8, y); y += line_h;

    ui.speed = make_label(s_scr, "Speed -- --%",
                                  &lv_font_montserrat_14, c->text_secondary);
    lv_obj_set_pos(ui.speed, 8, y);
}

#elif CFG_UI_STYLE == STYLE_DASHBOARD
// ===== 仪表盘风格 =====
static void build_dashboard_ui(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // 标题
    lv_obj_t *title = make_label(s_scr, "Bambu Monitor",
                                  &lv_font_montserrat_14, c->accent);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // 连接状态
    lv_obj_t *status = make_label(s_scr, "...",
                                   &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(status, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_set_user_data(status, (void *)9);

    // 喷嘴温度弧线（用简单的圆形进度条模拟）
    int cx = 70, cy = 110, r = 45;

    lv_obj_t *nozzle_arc_bg = lv_obj_create(s_scr);
    lv_obj_set_pos(nozzle_arc_bg, cx - r, cy - r);
    lv_obj_set_size(nozzle_arc_bg, r * 2, r * 2);
    lv_obj_set_style_bg_color(nozzle_arc_bg, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(nozzle_arc_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(nozzle_arc_bg, lv_color_hex(c->gauge_nozzle), 0);
    lv_obj_set_style_border_width(nozzle_arc_bg, 3, 0);

    lv_obj_t *nozzle_lbl = make_label(s_scr, "--°C",
                                       &lv_font_montserrat_20, c->gauge_nozzle);
    lv_obj_center(nozzle_lbl);
    // 注意: 这里简化处理，实际弧线需要 lv_arc 组件
    // 由于 LVGL 9.x 的 lv_arc API 变化，这里用文本 + 边框模拟
    lv_obj_align(nozzle_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_user_data(nozzle_lbl, (void *)1);

    lv_obj_t *nozzle_target = make_label(s_scr, "Target: --°C",
                                          &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(nozzle_target, LV_ALIGN_CENTER, 0, 10);

    // 热床温度
    cx = 170;
    lv_obj_t *bed_arc_bg = lv_obj_create(s_scr);
    lv_obj_set_pos(bed_arc_bg, cx - r, cy - r);
    lv_obj_set_size(bed_arc_bg, r * 2, r * 2);
    lv_obj_set_style_bg_color(bed_arc_bg, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_radius(bed_arc_bg, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(bed_arc_bg, lv_color_hex(c->gauge_bed), 0);
    lv_obj_set_style_border_width(bed_arc_bg, 3, 0);

    lv_obj_t *bed_lbl = make_label(s_scr, "--°C",
                                    &lv_font_montserrat_20, c->gauge_bed);
    lv_obj_align(bed_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_user_data(bed_lbl, (void *)2);

    lv_obj_t *bed_target = make_label(s_scr, "Target: --°C",
                                       &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(bed_target, LV_ALIGN_CENTER, 0, 10);

    // 进度条（底部）
    int bar_y = 175;
    lv_obj_t *bar_bg = lv_obj_create(s_scr);
    lv_obj_set_pos(bar_bg, 16, bar_y);
    lv_obj_set_size(bar_bg, 208, 24);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);
    lv_obj_set_style_radius(bar_bg, 12, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);

    lv_obj_t *bar = lv_bar_create(bar_bg);
    lv_obj_set_size(bar, 204, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), 0);
    lv_obj_set_style_radius(bar, 10, 0);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_user_data(bar_bg, (void *)10);

    lv_obj_t *percent = make_label(s_scr, "--%",
                                    &lv_font_montserrat_20, c->accent);
    lv_obj_align(percent, LV_ALIGN_CENTER, 0, bar_y + 30);
    lv_obj_set_user_data(percent, (void *)5);

    // 层数和状态
    lv_obj_t *layer = make_label(s_scr, "Layer --/--",
                                  &lv_font_montserrat_14, c->text_primary);
    lv_obj_align(layer, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_user_data(layer, (void *)4);

    lv_obj_t *state = make_label(s_scr, "IDLE",
                                  &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(state, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_user_data(state, (void *)7);
}

#elif CFG_UI_STYLE == STYLE_CARD || CFG_UI_STYLE == STYLE_CUTE
// ===== 卡片风格 / 可爱风格 =====
static void build_card_ui(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);

    // 顶部标题栏
    lv_obj_t *title_bar = lv_obj_create(s_scr);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_size(title_bar, 240, 32);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(c->card_bg), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 4, 0);

    const char *style_name = (CFG_UI_STYLE == STYLE_CUTE) ? "Bambu Cute" : "Bambu";
    lv_obj_t *title = make_label(title_bar, style_name,
                                  &lv_font_montserrat_14, c->accent);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 8, 0);

    ui.status = make_label(title_bar, "...",
                                   &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(ui.status, LV_ALIGN_RIGHT_MID, -8, 0);

    // 页码指示
    s_page_indicator = make_label(s_scr, "1/3",
                                           &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(s_page_indicator, LV_ALIGN_BOTTOM_RIGHT, -8, -4);

    // 内容区域容器（翻页时清空重建）
    s_content_area = lv_obj_create(s_scr);
    lv_obj_set_pos(s_content_area, 0, 34);
    lv_obj_set_size(s_content_area, 240, 182);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content_area, 0, 0);
    lv_obj_set_style_radius(s_content_area, 0, 0);
    lv_obj_set_style_pad_all(s_content_area, 0, 0);
    lv_obj_remove_flag(s_content_area, LV_OBJ_FLAG_SCROLLABLE);

    // 页面内容在 rebuild_page() 中根据 s_card_page 动态构建
    s_card_page = 0;
}
#endif

// ---------------------------------------------------------------------------
// 卡片风格翻页
// ---------------------------------------------------------------------------
#if CFG_UI_STYLE == STYLE_CARD || CFG_UI_STYLE == STYLE_CUTE

// 页面 0: 主状态（进度、状态、温度、层数、速度）
static void build_page0_content(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    int y = 6;

    ui.percent = make_label(s_content_area, "--%",
                            &lv_font_montserrat_48, c->accent);
    lv_obj_align(ui.percent, LV_ALIGN_TOP_MID, 0, y);
    y += 56;

    // 进度条
    lv_obj_t *bar_bg = lv_obj_create(s_content_area);
    lv_obj_set_pos(bar_bg, 16, y);
    lv_obj_set_size(bar_bg, 208, 16);
    lv_obj_set_style_bg_color(bar_bg, lv_color_hex(c->border), 0);
    lv_obj_set_style_radius(bar_bg, 8, 0);
    lv_obj_set_style_pad_all(bar_bg, 0, 0);
    lv_obj_set_style_border_width(bar_bg, 0, 0);

    lv_obj_t *bar = lv_bar_create(bar_bg);
    lv_obj_set_size(bar, 204, 12);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(c->accent), 0);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_bar_set_range(bar, 0, 100);
    ui.bar = bar;
    y += 24;

    ui.state = make_label(s_content_area, "IDLE",
                          &lv_font_montserrat_20, c->text_secondary);
    lv_obj_align(ui.state, LV_ALIGN_TOP_MID, 0, y);
    y += 30;

    ui.nozzle = make_label(s_content_area, "Nozzle --/--°C",
                           &lv_font_montserrat_14, c->gauge_nozzle);
    lv_obj_align(ui.nozzle, LV_ALIGN_TOP_LEFT, 16, y);

    ui.bed = make_label(s_content_area, "Bed --/--°C",
                        &lv_font_montserrat_14, c->gauge_bed);
    lv_obj_align(ui.bed, LV_ALIGN_TOP_RIGHT, -16, y);
    y += 24;

    ui.layer = make_label(s_content_area, "Layer --/--",
                          &lv_font_montserrat_14, c->text_primary);
    lv_obj_align(ui.layer, LV_ALIGN_TOP_LEFT, 16, y);

    ui.remain = make_label(s_content_area, "Remain --",
                           &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(ui.remain, LV_ALIGN_TOP_RIGHT, -16, y);
    y += 24;

    ui.speed = make_label(s_content_area, "Speed -- --%",
                          &lv_font_montserrat_14, c->text_secondary);
    lv_obj_align(ui.speed, LV_ALIGN_TOP_LEFT, 16, y);
}

// 页面 1: 温度详情 + 风扇
static void build_page1_content(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    int y = 10;

    ui.nozzle = make_label(s_content_area, "Nozzle --/--°C",
                           &lv_font_montserrat_20, c->gauge_nozzle);
    lv_obj_set_pos(ui.nozzle, 16, y); y += 32;

    ui.bed = make_label(s_content_area, "Bed --/--°C",
                        &lv_font_montserrat_20, c->gauge_bed);
    lv_obj_set_pos(ui.bed, 16, y); y += 32;

    ui.chamber = make_label(s_content_area, "Chamber --°C",
                            &lv_font_montserrat_20, c->gauge_chamber);
    lv_obj_set_pos(ui.chamber, 16, y); y += 40;

    // 风扇信息
    lv_obj_t *fan_title = make_label(s_content_area, "Fans",
                                     &lv_font_montserrat_14, c->accent);
    lv_obj_set_pos(fan_title, 16, y); y += 22;

    lv_obj_t *fan1 = make_label(s_content_area, "Cooling --%",
                                &lv_font_montserrat_14, c->text_primary);
    lv_obj_set_pos(fan1, 16, y); y += 22;
    ui.fan1 = fan1;

    lv_obj_t *fan2 = make_label(s_content_area, "Part Fan --%",
                                &lv_font_montserrat_14, c->text_primary);
    lv_obj_set_pos(fan2, 16, y);
    ui.fan2 = fan2;
}

// 页面 2: AMS 信息
static void build_page2_content(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    int y = 10;

    lv_obj_t *title = make_label(s_content_area, "AMS",
                                 &lv_font_montserrat_20, c->accent);
    lv_obj_set_pos(title, 16, y); y += 30;

    bambu_state_t *st = &g_bambu_state;
    if (st->ams_count > 0) {
        for (int i = 0; i < BAMBU_AMS_TRAY_COUNT; i++) {
            bambu_ams_tray_t *t = &st->trays[i];
            char buf[48];
            if (t->type[0]) {
                snprintf(buf, sizeof(buf), "#%d %s %s %d%%",
                         i + 1, t->type, t->color, (int)t->remain);
            } else {
                snprintf(buf, sizeof(buf), "#%d (empty)", i + 1);
            }
            lv_obj_t *lbl = make_label(s_content_area, buf,
                                       &lv_font_montserrat_14,
                                       t->active ? c->accent : c->text_secondary);
            lv_obj_set_pos(lbl, 16, y);
            y += 24;
        }
    } else {
        lv_obj_t *no_ams = make_label(s_content_area, "No AMS detected",
                                      &lv_font_montserrat_14, c->text_secondary);
        lv_obj_set_pos(no_ams, 16, y);
    }
}

static void update_page_indicator(void) {
    if (s_page_indicator) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d/%d", s_card_page + 1, CARD_PAGE_COUNT);
        lv_label_set_text(s_page_indicator, buf);
    }
}

static void rebuild_page(void) {
    // 清空内容区域和 UI 指针
    if (s_content_area) {
        lv_obj_clean(s_content_area);
    }
    memset(&ui, 0, sizeof(ui));

    // 根据页码重建内容
    switch (s_card_page) {
        case 0: build_page0_content(); break;
        case 1: build_page1_content(); break;
        case 2: build_page2_content(); break;
    }
    update_page_indicator();
}

static void next_page(void) {
    s_card_page = (s_card_page + 1) % CARD_PAGE_COUNT;
    rebuild_page();
    ESP_LOGI(TAG, "翻页到 %d/%d", s_card_page + 1, CARD_PAGE_COUNT);
}

static void prev_page(void) {
    s_card_page = (s_card_page - 1 + CARD_PAGE_COUNT) % CARD_PAGE_COUNT;
    rebuild_page();
    ESP_LOGI(TAG, "翻页到 %d/%d", s_card_page + 1, CARD_PAGE_COUNT);
}
#endif

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
void ui_monitor_enter(void) {
    s_scr = lv_obj_create(NULL);
    lv_screen_load(s_scr);

#if CFG_UI_STYLE == STYLE_COMPACT
    build_compact_ui();
#elif CFG_UI_STYLE == STYLE_DASHBOARD
    build_dashboard_ui();
#elif CFG_UI_STYLE == STYLE_CARD || CFG_UI_STYLE == STYLE_CUTE
    build_card_ui();
    rebuild_page();  // 构建初始页面内容
#endif

    // 启动定时刷新（每秒一次）
    s_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, NULL);

    ESP_LOGI(TAG, "监控页面已加载 (style=%d)", CFG_UI_STYLE);
}

void ui_monitor_exit(void) {
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (s_scr) {
        lv_obj_delete(s_scr);
        s_scr = NULL;
    }
    // 清除所有 UI 指针
    memset(&ui, 0, sizeof(ui));
    s_content_area = NULL;
    s_page_indicator = NULL;
}

void ui_monitor_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    switch (btn) {
        case BSP_BTN_UP:
#if CFG_UI_STYLE == STYLE_CARD || CFG_UI_STYLE == STYLE_CUTE
            prev_page();
#endif
            break;
        case BSP_BTN_DOWN:
#if CFG_UI_STYLE == STYLE_CARD || CFG_UI_STYLE == STYLE_CUTE
            next_page();
#endif
            break;
        case BSP_BTN_OK:
            // 刷新数据
            bambu_mqtt_pushall();
            break;
        default:
            break;
    }
}
