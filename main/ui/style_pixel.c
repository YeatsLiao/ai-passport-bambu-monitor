// main/ui/style_pixel.c —— 风格7: 像素机器人风 (移植自 ai-passport 官网 UI)
//
// 素材来源: ai-passport 仓库 main/ui_pixel.c + main/ui_pixel.h (同作者的原创像素风)
// 布局: Page0 = 像素纸牌面板 (墨黑硬投影 + 米白纸牌) + 会眨眼的电视机器人
//            + 超大百分比 + 描边进度条 + 4 行彩色方块数据
//       Page1 = AMS 列表 (方形色片 + 当前料槽整行黄色高亮, 复刻官网选中态)
// 屏幕装饰: 天空蓝底 + 右上像素云 + 底部草地 (亮绿顶 + 草皮块 + 泥土)
// 翻页策略: 两页面板同时创建, 翻页只切换显示/隐藏 (不 destroy/rebuild)
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

static const char *TAG __attribute__((unused)) = "style_pixel";

#define HAS_AMS   1
#define SLOT_COUNT 4                 // Page0 数据行数

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

// ── 像素风专属装饰色 (取自 ai-passport main/ui_pixel.h) ──
// 通用语义色 (墨黑/纸白/天空/草绿/黄/橙) 一律走 ui_theme 字段, 这里只放主题结构
// 表达不了的草地层次与机器人机身色, 集中声明以免魔数散落在布局代码里。
#define PX_GRASS_LIT  0xA7D93E       // 草地顶部亮绿
#define PX_GRASS_DARK 0x55951D       // 草皮深绿
#define PX_SOIL       0x75452E       // 泥土棕
#define PX_CLOUD      0xFFFFFF       // 云
#define PX_BODY       0x7557D9       // 机器人机身紫
#define PX_SCREEN     0xB9F3FF       // 机器人屏幕脸
#define PX_EYE        0x294B7A       // 机器人眼睛

// 组件顺序: config.h 定义 CFG_COMPONENT_ORDER (如 {5,4,1,2,6,8}) 则覆盖默认值。
// 注意两个分支都要定义 s_order: 上面写法若漏掉 #else 分支, 一旦用户定义了
// CFG_COMPONENT_ORDER 就会报 s_order undeclared (实测踩过)。
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

static lv_obj_t *s_card[2] = {NULL, NULL};      // 两页的透明容器 (含阴影+面板)

// Page 0
static lv_obj_t *s_mascot    = NULL;            // 像素机器人
static lv_obj_t *s_pct_lbl   = NULL;            // 超大百分比
static lv_obj_t *s_prog      = NULL;            // 描边进度条
static lv_obj_t *s_state_lbl = NULL;            // 状态行
static int       s_slot_cmp[SLOT_COUNT];        // 每行对应的组件
static lv_obj_t *s_slot_lbl[SLOT_COUNT];        // 图标 + 名称
static lv_obj_t *s_slot_val[SLOT_COUNT];        // 右对齐数值

// Page 1
static lv_obj_t *s_ams_hl[5];                   // 整行高亮底块
static lv_obj_t *s_ams_chip[5];                 // 方形色片
static lv_obj_t *s_ams_lbl[5];                  // 类型文字
static lv_obj_t *s_ams_pct[5];                  // 右对齐余量

// 标题栏 / 底部栏
static lv_obj_t *s_time_lbl = NULL;
static lv_obj_t *s_bat_lbl  = NULL;
static lv_obj_t *s_bat_fill = NULL;   // 像素电池填充块 (宽度随电量伸缩)
static lv_obj_t *s_pg_lbl   = NULL;

// 打印完成瞬间让机器人跳一下, 需要记住上一次状态
static bambu_print_state_t s_last_state = BAMBU_STATE_IDLE;

// ---------------------------------------------------------------------------
// 基础工具
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

// 像素方块: 直角、无描边、无内边距 —— 所有像素造型都由它拼出来
static lv_obj_t *block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color) {
    if (!parent) return NULL;
    lv_obj_t *o = lv_obj_create(parent);
    if (!o) return NULL;
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// 像素纸牌面板: 右下偏移的墨黑硬投影 (无模糊) + 米白纸牌 + 墨黑粗描边
// 返回纸牌本体, 投影作为其兄弟节点先画在下面。
static lv_obj_t *mk_panel(lv_obj_t *parent, int x, int y, int w, int h) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    block(parent, x + 5, y + 6, w, h, c->border);          // 硬投影

    lv_obj_t *p = block(parent, x, y, w, h, c->card_bg);
    if (!p) return NULL;
    lv_obj_set_style_border_color(p, lv_color_hex(c->border), 0);
    lv_obj_set_style_border_width(p, 4, 0);
    lv_obj_set_style_pad_all(p, 7, 0);
    return p;
}

// 像素云 (官网 add_cloud 原样移植: 墨黑底衬 + 三团白色方块)
static void add_cloud(lv_obj_t *parent, int x, int y) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    block(parent, x + 1,  y + 7, 43, 10, c->border);
    block(parent, x + 5,  y + 4, 35, 10, PX_CLOUD);
    block(parent, x + 12, y,     10,  9, PX_CLOUD);
    block(parent, x + 27, y + 1,  9,  8, PX_CLOUD);
}

// ---------------------------------------------------------------------------
// 像素机器人 (官网 ui_pixel_mascot_create 原样移植, 38x48)
// ---------------------------------------------------------------------------
static void blink_eye(void *obj, int32_t value) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)value, 0);
}

// 双眼循环眨眼: 70ms 闭眼 + 70ms 睁眼, 每 1700ms 来一次
static void start_blink(lv_obj_t *eye) {
    if (!eye) return;
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, eye);
    lv_anim_set_exec_cb(&anim, blink_eye);
    lv_anim_set_values(&anim, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&anim, 70);
    lv_anim_set_playback_duration(&anim, 70);
    lv_anim_set_repeat_delay(&anim, 1700);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

static void jump_y(void *obj, int32_t value) {
    lv_obj_set_y((lv_obj_t *)obj, value);
}

// 跳一下 (官网 ui_pixel_mascot_jump): 打印完成时触发
static void mascot_jump(lv_obj_t *m) {
    if (!m) return;
    int y = lv_obj_get_y(m);
    lv_anim_delete(m, jump_y);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, m);
    lv_anim_set_exec_cb(&anim, jump_y);
    lv_anim_set_values(&anim, y, y - 5);
    lv_anim_set_duration(&anim, 110);
    lv_anim_set_playback_duration(&anim, 140);
    lv_anim_set_path_cb(&anim, lv_anim_path_step);
    lv_anim_start(&anim);
}

// 原创"小电视机器人": 天线 + 发光屏幕脸 + 橙色围巾 + 紫色机身 + 履带脚
static lv_obj_t *mk_mascot(lv_obj_t *parent, int x, int y) {
    const ui_theme_colors_t *c = ui_theme_get_colors();
    if (!parent) return NULL;

    lv_obj_t *m = lv_obj_create(parent);
    if (!m) return NULL;
    lv_obj_remove_flag(m, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(m, x, y);
    lv_obj_set_size(m, 38, 48);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);

    block(m, 18, 0,  3,  6, c->border);       // 天线杆
    block(m, 16, 0,  7,  3, c->accent);       // 天线球 (围巾橙)
    block(m, 3,  6,  32, 24, c->border);      // 头部外框
    block(m, 0,  12, 5,  10, PX_BODY);        // 左耳
    block(m, 33, 12, 5,  10, PX_BODY);        // 右耳
    block(m, 7,  10, 24, 16, PX_SCREEN);      // 屏幕脸
    lv_obj_t *left_eye  = block(m, 11, 14, 4, 6, PX_EYE);
    lv_obj_t *right_eye = block(m, 23, 14, 4, 6, PX_EYE);
    block(m, 16, 22, 7,  2,  PX_BODY);        // 嘴
    block(m, 10, 29, 18, 4,  c->accent);      // 围巾
    block(m, 8,  33, 22, 11, PX_BODY);        // 机身
    block(m, 3,  35, 5,  7,  PX_SCREEN);      // 左臂
    block(m, 30, 35, 5,  7,  PX_SCREEN);      // 右臂
    block(m, 8,  44, 9,  4,  c->border);      // 左履带
    block(m, 21, 44, 9,  4,  c->border);      // 右履带

    start_blink(left_eye);
    start_blink(right_eye);
    return m;
}

// ---------------------------------------------------------------------------
// 数据映射
// ---------------------------------------------------------------------------
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

// 方块项目符号颜色: 温度类沿用各自仪表色, 其余用强调色
static uint32_t cmp_color(int cmp, const ui_theme_colors_t *c) {
    switch (cmp) {
        case CMP_NOZZLE:  return c->gauge_nozzle;
        case CMP_BED:     return c->gauge_bed;
        case CMP_CHAMBER: return c->gauge_chamber;
        default:          return c->accent;
    }
}

// 组件编号 -> 数值文本。
// 本风格的数值标签用 L_FONT_TEXT (中文下是裁剪版 Noto Sans SC), 所以剩余时间
// 可以本地化成 "1时20分"。对比 style_white 的大字号数值只有 Montserrat, 那边
// 必须输出纯 ASCII 才不会变占位方块。
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
            snprintf(buf, n, "%d%%", st->spd_mag);
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
// Page 0: 像素纸牌 + 机器人 + 进度条 + 数据行
// ---------------------------------------------------------------------------
static void build_page0(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();

    // 透明容器: 阴影块与纸牌都放在里面, 翻页时一起隐藏才不会留下孤影
    lv_obj_t *page = lv_obj_create(s_content_area);
    if (!page) return;
    s_card[0] = page;
    lv_obj_set_pos(page, 8, 34);
    lv_obj_set_size(page, 224, 218);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    lv_obj_t *panel = mk_panel(page, 0, 0, 214, 208);   // 内容区 192x186
    if (!panel) return;

    // ── 机器人 (左) + 超大百分比 (右) ──
    s_mascot  = mk_mascot(panel, 0, 0);
    s_pct_lbl = mk_lbl(panel, "--%", L_FONT_NUM_HUGE, c->text_primary);
    if (s_pct_lbl) lv_obj_align(s_pct_lbl, LV_ALIGN_TOP_RIGHT, 0, -4);

    // ── 描边进度条 (直角 + 墨黑外框 + 橙色填充) ──
    s_prog = lv_bar_create(panel);
    if (s_prog) {
        lv_obj_set_pos(s_prog, 0, 56);
        lv_obj_set_size(s_prog, 192, 14);
        lv_obj_remove_flag(s_prog, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(s_prog, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_prog, lv_color_hex(c->card_bg), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_prog, 2, LV_PART_MAIN);
        lv_obj_set_style_border_color(s_prog, lv_color_hex(c->border), LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_prog, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(s_prog, 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(s_prog, lv_color_hex(c->accent), LV_PART_INDICATOR);
        lv_bar_set_range(s_prog, 0, 100);
        lv_bar_set_value(s_prog, 0, LV_ANIM_OFF);
    }

    // ── 状态行 ──
    s_state_lbl = mk_lbl(panel, LV_SYMBOL_WIFI " " L_CONNECTING, L_FONT_TEXT, c->text_secondary);
    if (s_state_lbl) lv_obj_set_pos(s_state_lbl, 0, 78);

    block(panel, 0, 100, 192, 2, c->border);            // 墨黑分隔线

    // ── 4 行数据 (由 CFG_COMPONENT_ORDER 驱动) ──
    // 百分比与状态已各占固定位置, 跳过它们和 AMS, 其余按配置取前 4 项
    static const int fallback_cmp[SLOT_COUNT] = {CMP_LAYER, CMP_NOZZLE, CMP_BED, CMP_REMAIN};
    int n = 0;
    for (int i = 0; i < (int)s_order_len && n < SLOT_COUNT; i++) {
        int cmp = s_order[i];
        if (cmp == CMP_PERCENT || cmp == CMP_STATE || cmp == CMP_AMS) continue;
        s_slot_cmp[n++] = cmp;
    }
    for (int i = n; i < SLOT_COUNT; i++) s_slot_cmp[i] = fallback_cmp[i];

    memset(s_slot_lbl, 0, sizeof(s_slot_lbl));
    memset(s_slot_val, 0, sizeof(s_slot_val));

    for (int i = 0; i < SLOT_COUNT; i++) {
        int cmp = s_slot_cmp[i];
        int y   = 108 + i * 19;

        // 彩色方块项目符号 (墨黑描边, 像素风); 颜色随组件语义固定, 运行时不变
        lv_obj_t *dot = block(panel, 0, y + 3, 8, 8, cmp_color(cmp, c));
        if (dot) {
            lv_obj_set_style_border_width(dot, 1, 0);
            lv_obj_set_style_border_color(dot, lv_color_hex(c->border), 0);
        }

        char lbl[48];
        snprintf(lbl, sizeof(lbl), "%s %s", ICO(cmp), cmp_name(cmp));
        s_slot_lbl[i] = mk_lbl(panel, lbl, L_FONT_TEXT, c->text_secondary);
        if (s_slot_lbl[i]) lv_obj_set_pos(s_slot_lbl[i], 13, y);

        // 数值右对齐: align 写的是样式, 文字变长后布局引擎会重新右对齐
        s_slot_val[i] = mk_lbl(panel, "--", L_FONT_TEXT, cmp_color(cmp, c));
        if (s_slot_val[i]) lv_obj_align(s_slot_val[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }
}

// ---------------------------------------------------------------------------
// Page 1: AMS 列表 (当前料槽整行黄色高亮, 复刻官网选中态)
// ---------------------------------------------------------------------------
static void build_page1(void) {
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_t *page = lv_obj_create(s_content_area);
    if (!page) return;
    s_card[1] = page;
    lv_obj_set_pos(page, 8, 34);
    lv_obj_set_size(page, 224, 218);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    lv_obj_t *panel = mk_panel(page, 0, 0, 214, 208);
    if (!panel) return;

    mk_lbl(panel, LV_SYMBOL_SD_CARD " " L_AMS, L_FONT_TEXT_BIG, c->text_primary);
    block(panel, 0, 28, 192, 2, c->border);

    memset(s_ams_hl, 0, sizeof(s_ams_hl));
    memset(s_ams_chip, 0, sizeof(s_ams_chip));
    memset(s_ams_lbl, 0, sizeof(s_ams_lbl));
    memset(s_ams_pct, 0, sizeof(s_ams_pct));

    for (int i = 0; i < 5; i++) {
        int y = 38 + i * 29;

        // 整行高亮底块 (未选中时全透明, 只留占位)
        lv_obj_t *hl = block(panel, 0, y - 2, 192, 26, c->warning);
        if (hl) lv_obj_set_style_bg_opa(hl, LV_OPA_TRANSP, 0);
        s_ams_hl[i] = hl;

        // 方形色片: 颜色由 MQTT tray_color 实时驱动 (透明料走空心描边分支)
        lv_obj_t *chip = block(panel, 4, y + 3, 14, 14, c->text_secondary);
        if (chip) {
            lv_obj_set_style_border_width(chip, 2, 0);
            lv_obj_set_style_border_color(chip, lv_color_hex(c->border), 0);
        }
        s_ams_chip[i] = chip;

        char buf[32];
        if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
        else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
        s_ams_lbl[i] = mk_lbl(panel, buf, L_FONT_TEXT, c->text_primary);
        if (s_ams_lbl[i]) lv_obj_set_pos(s_ams_lbl[i], 24, y + 2);

        s_ams_pct[i] = mk_lbl(panel, "--", L_FONT_TEXT, c->text_secondary);
        if (s_ams_pct[i]) lv_obj_align(s_ams_pct[i], LV_ALIGN_TOP_RIGHT, -4, y + 2);
    }
}

// ---------------------------------------------------------------------------
// 构建整个屏幕 (首次创建所有持久对象)
// ---------------------------------------------------------------------------
void style_pixel_build(void) {
    if (!s_scr) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    if (s_card[0] || s_card[1]) return;   // 已创建过, 跳过

    lv_obj_set_style_bg_color(s_scr, lv_color_hex(c->bg), 0);   // 天空

    // ── 标题栏 (持久): 天空底 + 像素纸牌标题 + 右侧像素电池 ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(c->header_bg), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // ── 像素方块电池 (复刻官网电池 UI): 墨黑外框 + 电量填充, 百分比在其左 ──
    block(header, 192, 8, 22, 14, c->border);                  // 外框
    block(header, 214, 12, 4, 6, c->border);                   // 正极帽
    block(header, 194, 10, 18, 10, c->header_bg);              // 内腔
    s_bat_fill = block(header, 194, 10, 18, 10, c->success);   // 填充 (宽度随电量)

    const uint32_t htxt = ui_theme_on_color(c->header_bg);
    s_time_lbl = mk_lbl(header, "--:--", L_FONT_NUM, htxt);
    if (s_time_lbl) lv_obj_align(s_time_lbl, LV_ALIGN_LEFT_MID, 88, 0);
    s_bat_lbl = mk_lbl(header, "--", L_FONT_NUM, htxt);
    if (s_bat_lbl) lv_obj_align(s_bat_lbl, LV_ALIGN_RIGHT_MID, -52, 0);   // 右缘 x188: 距时间与电池框各留 4px

    // 官网标题牌: 墨黑投影 + 米白纸牌 + 墨黑描边, 牌内标题用墨字
    block(header, 5, 6, 74, 22, c->border);
    lv_obj_t *plate = block(header, 3, 4, 74, 22, c->card_bg);
    if (plate) {
        lv_obj_set_style_border_color(plate, lv_color_hex(c->border), 0);
        lv_obj_set_style_border_width(plate, 2, 0);
        lv_obj_t *title = mk_lbl(plate, L_TITLE_BAMBU, L_FONT_TEXT, c->text_primary);
        if (title) lv_obj_center(title);
    }

    // ── 底部栏 (持久): 草地 (亮绿顶 + 草皮块 + 泥土) ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(c->footer_bg), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 0, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    block(footer, 0, 0, 240, 3, PX_GRASS_LIT);
    for (int x = 0; x < 240; x += 30) {
        block(footer, x,      22, 18, 8, PX_GRASS_DARK);
        block(footer, x + 18, 26, 12, 4, PX_SOIL);
    }

    // 云朵装饰移到草地上方 (原在标题栏, 让位给像素电池)
    add_cloud(footer, 148, 3);

    // 草地上文字按背景亮度自动取色 (草绿 -> 墨字)
    const uint32_t ftxt = ui_theme_on_color(c->footer_bg);
    lv_obj_t *nav = mk_lbl(footer, L_NAV_HINT, L_FONT_TEXT, ftxt);
    if (nav) lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, -3);

    char pg[16];
    snprintf(pg, sizeof(pg), "%d/%d", s_page + 1, s_total_pages);
    s_pg_lbl = mk_lbl(footer, pg, L_FONT_NUM, ftxt);
    if (s_pg_lbl) lv_obj_align(s_pg_lbl, LV_ALIGN_RIGHT_MID, -8, -3);

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
void style_pixel_update(void) {
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
        if (soc >= 0) snprintf(buf, sizeof(buf), "%d%%", soc);
        else          snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(s_bat_lbl, buf);
        // 百分比文字颜色随电量分档, 与填充块同色
        lv_obj_set_style_text_color(s_bat_lbl, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }
    // 像素电池填充: 宽度随电量伸缩, 颜色随电量分档 (满电草地绿 / 低电红)
    if (s_bat_fill) {
        int soc = bsp_battery_soc();
        int w = (soc > 0) ? 18 * soc / 100 : 0;
        if (soc >= 0 && w < 2) w = 2;   // 有数据但极低时留 2px 可见
        lv_obj_set_width(s_bat_fill, w);
        lv_obj_set_style_bg_color(s_bat_fill, lv_color_hex(ui_theme_battery_color(soc)), 0);
    }

    // 打印完成瞬间让机器人跳一下
    if (st->state != s_last_state) {
        if (st->state == BAMBU_STATE_FINISH) mascot_jump(s_mascot);
        s_last_state = st->state;
    }

    // ── Page 0 ──
    if (s_page == 0) {
        if (s_prog) lv_bar_set_value(s_prog, st->mc_percent, LV_ANIM_ON);

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

        for (int i = 0; i < SLOT_COUNT; i++) {
            if (!s_slot_val[i]) continue;
            cmp_value(buf, sizeof(buf), s_slot_cmp[i], st);
            lv_label_set_text(s_slot_val[i], buf);
        }
    }

    // ── Page 1: AMS 列表 ──
    if (s_page == 1) {
        for (int i = 0; i < 5; i++) {
            if (!s_ams_lbl[i]) continue;
            if (i < 4 && i >= st->ams_count) continue;
            bambu_ams_tray_t *t = (i < 4) ? &st->trays[i] : &st->vt_tray;

            if (t->type[0]) {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, t->type);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, t->type);
            } else {
                if (i < 4) snprintf(buf, sizeof(buf), "#%d %s", i + 1, L_EMPTY);
                else       snprintf(buf, sizeof(buf), "%s %s", L_EXT, L_EMPTY);
            }
            lv_label_set_text(s_ams_lbl[i], buf);

            // 当前料槽整行黄色高亮 (官网 ui_pixel_set_selected 的选中态)
            if (s_ams_hl[i])
                lv_obj_set_style_bg_opa(s_ams_hl[i],
                                        t->active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
            if (s_ams_pct[i]) {
                snprintf(buf, sizeof(buf), "%d%%", (int)t->remain);
                lv_label_set_text(s_ams_pct[i], buf);
            }
            if (s_ams_chip[i]) ui_theme_tray_swatch(s_ams_chip[i], t);
        }
    }
}

// ---------------------------------------------------------------------------
// 翻页 (显示/隐藏切换, 不 destroy/rebuild)
// ---------------------------------------------------------------------------
int style_pixel_page_count(void) { return s_total_pages; }
int style_pixel_current_page(void) { return s_page; }

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

void style_pixel_next_page(void) {
    s_page = (s_page + 1) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}

void style_pixel_prev_page(void) {
    s_page = (s_page - 1 + s_total_pages) % s_total_pages;
    ESP_LOGI(TAG, "翻页 -> page %d/%d", s_page + 1, s_total_pages);
    show_page(s_page);
}
