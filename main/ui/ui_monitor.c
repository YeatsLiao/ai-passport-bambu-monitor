// main/ui/ui_monitor.c —— 监控页面框架
//
// 职责:
//   1. 管理 LVGL 屏幕生命周期 (enter/exit)
//   2. 定时刷新 (1s → 调用当前风格的 update)
//   3. 按键分发 (UP/DOWN 翻页, OK 刷新)
//   4. 页面重建调度 (rebuild_page → 清空屏幕 → 调用当前风格的 build)
//
// 具体 UI 渲染由各 style_xxx.c 实现。

#include "ui_monitor.h"
#include "ui_theme.h"
#include "ui_lang.h"
#include "../app_config.h"
#include "../bambu_state.h"
#include "../bambu_mqtt.h"

#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ui_monitor";

lv_obj_t *s_scr = NULL;
static lv_timer_t *s_refresh_timer = NULL;
lv_obj_t *s_content_area  = NULL;   // 内容容器
static lv_obj_t *s_setup_overlay = NULL;   // 首次使用配置指引卡 (NULL=未显示)

// ---------------------------------------------------------------------------
// 风格注册表: 全部 12 套风格都编译进固件, 运行时按 app_config.ui_style 选择
// 索引 = STYLE_* 编号 - 1 (0..11)
// ---------------------------------------------------------------------------
typedef struct {
    void (*build)(void);
    void (*update)(void);
    int  (*page_count)(void);
    int  (*cur_page)(void);
    void (*next)(void);
    void (*prev)(void);
} style_ops_t;

#define STYLE_OPS(name) { \
    style_##name##_build, style_##name##_update, style_##name##_page_count, \
    style_##name##_current_page, style_##name##_next_page, style_##name##_prev_page \
}

static const style_ops_t s_styles[12] = {
    STYLE_OPS(bambu),      // 1  STYLE_BAMBU
    STYLE_OPS(cyber),      // 2  STYLE_CYBER
    STYLE_OPS(sheikah),    // 3  STYLE_SHEIKAH
    STYLE_OPS(white),      // 4  STYLE_WHITE
    STYLE_OPS(industrial), // 5  STYLE_INDUSTRIAL
    STYLE_OPS(neon),       // 6  STYLE_NEON
    STYLE_OPS(pixel),      // 7  STYLE_PIXEL
    STYLE_OPS(ssd),        // 8  STYLE_SSD
    STYLE_OPS(f1),         // 9  STYLE_F1
    STYLE_OPS(gauge),      // 10 STYLE_GAUGE
    STYLE_OPS(geist),      // 11 STYLE_GEIST
    STYLE_OPS(apple),      // 12 STYLE_APPLE
};

// 当前风格操作集 (配置值非法时回退 Bambu)
static const style_ops_t *cur_style(void) {
    uint8_t s = app_config_get()->ui_style;
    if (s < 1 || s > 12) s = STYLE_BAMBU;
    return &s_styles[s - 1];
}

// ---------------------------------------------------------------------------
// 页面重建（只清理内容区域，标题栏和底部栏保持不变）
// ---------------------------------------------------------------------------
void rebuild_page(void) {
    // 当前风格使用显示/隐藏切换, 此函数保留供兼容
    const style_ops_t *st = cur_style();
    st->build();
    ESP_LOGI(TAG, "页面已重建 (style=%s, page=%d/%d)",
             ui_theme_style_name(),
             st->cur_page() + 1, st->page_count());
}

// ---------------------------------------------------------------------------
// 首次使用配置指引: 刷入他人固件 / config.h 未配置时 WiFi 无法连接,
// MQTT 启动 15s 超时后由 main.c 调用, 在监控页上方叠加全屏指引卡。
// 连接成功后由刷新定时器自动撤除, 无需按键。
// ---------------------------------------------------------------------------
void ui_monitor_show_setup_hint(void) {
    if (!s_scr || s_setup_overlay) return;
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_t *ov = lv_obj_create(s_scr);
    if (!ov) return;
    s_setup_overlay = ov;
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_size(ov, 240, 320);
    lv_obj_set_style_bg_color(ov, lv_color_hex(c->bg), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 16, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ov);

    // 警告图标 (图标字形只在 Montserrat, 用 SYMBOL 字体)
    lv_obj_t *ico = lv_label_create(ov);
    if (ico) {
        lv_label_set_text(ico, LV_SYMBOL_WARNING);
        lv_obj_set_style_text_font(ico, L_FONT_SYMBOL_BIG, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(c->warning), 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 40);
    }

    // 标题
    lv_obj_t *title = lv_label_create(ov);
    if (title) {
        lv_label_set_text(title, L_SETUP_TITLE);
        lv_obj_set_style_text_font(title, L_FONT_TEXT_BIG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 86);
    }

    // 正文 (自动换行, 预留左右内边距后的宽度)
    lv_obj_t *body = lv_label_create(ov);
    if (body) {
        lv_label_set_text(body, L_SETUP_HINT);
        lv_obj_set_style_text_font(body, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(body, lv_color_hex(c->text_secondary), 0);
        lv_obj_set_width(body, 208);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 124);
    }

    ESP_LOGW(TAG, "显示首次使用配置指引 (MQTT 未连接)");
}

// ---------------------------------------------------------------------------
// 配网模式指引: 全屏覆盖, 显示热点名与配置页地址。
// 用户保存配置后设备自动重启, 因此无需撤除逻辑。
// ---------------------------------------------------------------------------
void ui_monitor_show_prov_mode(const char *ap_ssid) {
    if (!s_scr) return;

    // 从失败指引切入配网时, 先撤掉旧指引卡避免叠加
    if (s_setup_overlay) {
        lv_obj_delete(s_setup_overlay);
        s_setup_overlay = NULL;
    }
    const ui_theme_colors_t *c = ui_theme_get_colors();

    lv_obj_t *ov = lv_obj_create(s_scr);
    if (!ov) return;
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_size(ov, 240, 320);
    lv_obj_set_style_bg_color(ov, lv_color_hex(c->bg), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ov, 0, 0);
    lv_obj_set_style_pad_all(ov, 16, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ov);

    // WiFi 图标 (图标字形只在 Montserrat, 用 SYMBOL 字体)
    lv_obj_t *ico = lv_label_create(ov);
    if (ico) {
        lv_label_set_text(ico, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_font(ico, L_FONT_SYMBOL_BIG, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(c->accent), 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 36);
    }

    // 标题
    lv_obj_t *title = lv_label_create(ov);
    if (title) {
        lv_label_set_text(title, L_PROV_TITLE);
        lv_obj_set_style_text_font(title, L_FONT_TEXT_BIG, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(c->text_primary), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 78);
    }

    // 步骤 1: 手机连接热点 (热点名大一号, 用主题强调色突出)
    lv_obj_t *s1 = lv_label_create(ov);
    if (s1) {
        lv_label_set_text_fmt(s1, "1. %s", L_PROV_STEP1);
        lv_obj_set_style_text_font(s1, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s1, lv_color_hex(c->text_secondary), 0);
        lv_obj_align(s1, LV_ALIGN_TOP_MID, 0, 122);
    }
    lv_obj_t *ssid_lbl = lv_label_create(ov);
    if (ssid_lbl) {
        lv_label_set_text(ssid_lbl, ap_ssid ? ap_ssid : "");
        lv_obj_set_style_text_font(ssid_lbl, L_FONT_NUM_BIG, 0);
        lv_obj_set_style_text_color(ssid_lbl, lv_color_hex(c->accent), 0);
        lv_obj_align(ssid_lbl, LV_ALIGN_TOP_MID, 0, 144);
    }

    // 步骤 2: 浏览器打开 (地址纯 ASCII, 用 Montserrat 数字字体清晰易读)
    lv_obj_t *s2 = lv_label_create(ov);
    if (s2) {
        lv_label_set_text_fmt(s2, "2. %s", L_PROV_STEP2);
        lv_obj_set_style_text_font(s2, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s2, lv_color_hex(c->text_secondary), 0);
        lv_obj_align(s2, LV_ALIGN_TOP_MID, 0, 192);
    }
    lv_obj_t *url = lv_label_create(ov);
    if (url) {
        lv_label_set_text(url, L_PROV_URL);
        lv_obj_set_style_text_font(url, L_FONT_NUM_BIG, 0);
        lv_obj_set_style_text_color(url, lv_color_hex(c->accent), 0);
        lv_obj_align(url, LV_ALIGN_TOP_MID, 0, 214);
    }

    // 步骤 3: 说明 (自动换行)
    lv_obj_t *s3 = lv_label_create(ov);
    if (s3) {
        lv_label_set_text_fmt(s3, "3. %s", L_PROV_STEP3);
        lv_obj_set_style_text_font(s3, L_FONT_TEXT, 0);
        lv_obj_set_style_text_color(s3, lv_color_hex(c->text_secondary), 0);
        lv_obj_set_width(s3, 208);
        lv_label_set_long_mode(s3, LV_LABEL_LONG_WRAP);
        lv_obj_align(s3, LV_ALIGN_TOP_MID, 0, 262);
    }

    ESP_LOGI(TAG, "显示配网模式指引 (热点: %s)", ap_ssid ? ap_ssid : "?");
    s_setup_overlay = ov;  // 记录指针, 让 refresh_timer 跳过背景刷新 + 后续可删除
}

// ---------------------------------------------------------------------------
// 定时刷新
// ---------------------------------------------------------------------------
static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
    // 指引卡在连接成功后自动撤除, 露出下方监控页
    if (s_setup_overlay && bambu_mqtt_connected()) {
        lv_obj_delete(s_setup_overlay);
        s_setup_overlay = NULL;
        ESP_LOGI(TAG, "MQTT 已连接, 撤除配置指引");
    }
    // 配网指引显示时跳过背景刷新 (省 CPU, 避免看门狗超时)
    if (s_setup_overlay) return;
    cur_style()->update();
}

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
void ui_monitor_enter(void) {
    s_scr = lv_obj_create(NULL);
    if (!s_scr) {
        ESP_LOGE(TAG, "屏幕创建失败！内存不足");
        return;
    }
    lv_screen_load(s_scr);

    // 由风格 build 函数创建标题栏/底部栏/内容区（使用主题颜色）
    cur_style()->build();

    s_refresh_timer = lv_timer_create(refresh_timer_cb, 1000, NULL);
    ESP_LOGI(TAG, "监控页面已加载 (style=%s)", ui_theme_style_name());
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
    s_content_area  = NULL;
}

void ui_monitor_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;

    switch (btn) {
        case BSP_BTN_UP:
            cur_style()->prev();
            break;
        case BSP_BTN_DOWN:
            cur_style()->next();
            break;
        case BSP_BTN_OK:
            bambu_mqtt_pushall();
            ESP_LOGI(TAG, "手动刷新 (pushall)");
            break;
        default:
            break;
    }
}
