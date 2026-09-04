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
// 定时刷新
// ---------------------------------------------------------------------------
static void refresh_timer_cb(lv_timer_t *timer) {
    (void)timer;
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
