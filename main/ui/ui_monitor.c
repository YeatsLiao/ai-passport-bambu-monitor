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
// 风格宏: 根据 CFG_UI_STYLE 选择对应的风格函数
// ---------------------------------------------------------------------------
#if CFG_UI_STYLE == STYLE_BAMBU
    #define STYLE_BUILD       style_bambu_build
    #define STYLE_UPDATE      style_bambu_update
    #define STYLE_PAGE_COUNT  style_bambu_page_count
    #define STYLE_CUR_PAGE    style_bambu_current_page
    #define STYLE_NEXT        style_bambu_next_page
    #define STYLE_PREV        style_bambu_prev_page
#elif CFG_UI_STYLE == STYLE_CYBER
    #define STYLE_BUILD       style_cyber_build
    #define STYLE_UPDATE      style_cyber_update
    #define STYLE_PAGE_COUNT  style_cyber_page_count
    #define STYLE_CUR_PAGE    style_cyber_current_page
    #define STYLE_NEXT        style_cyber_next_page
    #define STYLE_PREV        style_cyber_prev_page
#elif CFG_UI_STYLE == STYLE_SHEIKAH
    #define STYLE_BUILD       style_sheikah_build
    #define STYLE_UPDATE      style_sheikah_update
    #define STYLE_PAGE_COUNT  style_sheikah_page_count
    #define STYLE_CUR_PAGE    style_sheikah_current_page
    #define STYLE_NEXT        style_sheikah_next_page
    #define STYLE_PREV        style_sheikah_prev_page
#elif CFG_UI_STYLE == STYLE_WHITE
    #define STYLE_BUILD       style_white_build
    #define STYLE_UPDATE      style_white_update
    #define STYLE_PAGE_COUNT  style_white_page_count
    #define STYLE_CUR_PAGE    style_white_current_page
    #define STYLE_NEXT        style_white_next_page
    #define STYLE_PREV        style_white_prev_page
#elif CFG_UI_STYLE == STYLE_INDUSTRIAL
    #define STYLE_BUILD       style_industrial_build
    #define STYLE_UPDATE      style_industrial_update
    #define STYLE_PAGE_COUNT  style_industrial_page_count
    #define STYLE_CUR_PAGE    style_industrial_current_page
    #define STYLE_NEXT        style_industrial_next_page
    #define STYLE_PREV        style_industrial_prev_page
#elif CFG_UI_STYLE == STYLE_NEON
    #define STYLE_BUILD       style_neon_build
    #define STYLE_UPDATE      style_neon_update
    #define STYLE_PAGE_COUNT  style_neon_page_count
    #define STYLE_CUR_PAGE    style_neon_current_page
    #define STYLE_NEXT        style_neon_next_page
    #define STYLE_PREV        style_neon_prev_page
#elif CFG_UI_STYLE == STYLE_PIXEL
    #define STYLE_BUILD       style_pixel_build
    #define STYLE_UPDATE      style_pixel_update
    #define STYLE_PAGE_COUNT  style_pixel_page_count
    #define STYLE_CUR_PAGE    style_pixel_current_page
    #define STYLE_NEXT        style_pixel_next_page
    #define STYLE_PREV        style_pixel_prev_page
#elif CFG_UI_STYLE == STYLE_SSD
    #define STYLE_BUILD       style_ssd_build
    #define STYLE_UPDATE      style_ssd_update
    #define STYLE_PAGE_COUNT  style_ssd_page_count
    #define STYLE_CUR_PAGE    style_ssd_current_page
    #define STYLE_NEXT        style_ssd_next_page
    #define STYLE_PREV        style_ssd_prev_page
#elif CFG_UI_STYLE == STYLE_F1
    #define STYLE_BUILD       style_f1_build
    #define STYLE_UPDATE      style_f1_update
    #define STYLE_PAGE_COUNT  style_f1_page_count
    #define STYLE_CUR_PAGE    style_f1_current_page
    #define STYLE_NEXT        style_f1_next_page
    #define STYLE_PREV        style_f1_prev_page
#elif CFG_UI_STYLE == STYLE_GAUGE
    #define STYLE_BUILD       style_gauge_build
    #define STYLE_UPDATE      style_gauge_update
    #define STYLE_PAGE_COUNT  style_gauge_page_count
    #define STYLE_CUR_PAGE    style_gauge_current_page
    #define STYLE_NEXT        style_gauge_next_page
    #define STYLE_PREV        style_gauge_prev_page
#elif CFG_UI_STYLE == STYLE_GEIST
    #define STYLE_BUILD       style_geist_build
    #define STYLE_UPDATE      style_geist_update
    #define STYLE_PAGE_COUNT  style_geist_page_count
    #define STYLE_CUR_PAGE    style_geist_current_page
    #define STYLE_NEXT        style_geist_next_page
    #define STYLE_PREV        style_geist_prev_page
#elif CFG_UI_STYLE == STYLE_APPLE
    #define STYLE_BUILD       style_apple_build
    #define STYLE_UPDATE      style_apple_update
    #define STYLE_PAGE_COUNT  style_apple_page_count
    #define STYLE_CUR_PAGE    style_apple_current_page
    #define STYLE_NEXT        style_apple_next_page
    #define STYLE_PREV        style_apple_prev_page
#endif

// ---------------------------------------------------------------------------
// 页面重建（只清理内容区域，标题栏和底部栏保持不变）
// ---------------------------------------------------------------------------
void rebuild_page(void) {
    // 当前风格使用显示/隐藏切换, 此函数保留供兼容
    STYLE_BUILD();
    ESP_LOGI(TAG, "页面已重建 (style=%s, page=%d/%d)",
             ui_theme_style_name(),
             STYLE_CUR_PAGE() + 1, STYLE_PAGE_COUNT());
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
        lv_label_set_text(s1, "1. " L_PROV_STEP1);
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
        lv_label_set_text(s2, "2. " L_PROV_STEP2);
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
        lv_label_set_text(s3, "3. " L_PROV_STEP3);
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
    STYLE_UPDATE();
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
    STYLE_BUILD();

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
            STYLE_PREV();
            break;
        case BSP_BTN_DOWN:
            STYLE_NEXT();
            break;
        case BSP_BTN_OK:
            bambu_mqtt_pushall();
            ESP_LOGI(TAG, "手动刷新 (pushall)");
            break;
        default:
            break;
    }
}
