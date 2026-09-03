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
lv_obj_t *s_content_area  = NULL;   // 内容容器（翻页时只清理这里）
static lv_obj_t *s_page_ind_lbl  = NULL;   // 底部栏页码标签

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
#endif

// ---------------------------------------------------------------------------
// 页面重建（只清理内容区域，标题栏和底部栏保持不变）
// ---------------------------------------------------------------------------
void rebuild_page(void) {
    if (!s_content_area) return;
    lv_obj_clean(s_content_area);     // 只删内容子对象
    STYLE_BUILD();

    // 更新底部栏页码
    if (s_page_ind_lbl) {
        char pg[16];
        snprintf(pg, sizeof(pg), "%d/%d", STYLE_CUR_PAGE() + 1, STYLE_PAGE_COUNT());
        lv_label_set_text(s_page_ind_lbl, pg);
    }

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

    // ── 标题栏（创建一次，持久存在） ──
    lv_obj_t *header = lv_obj_create(s_scr);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, 240, 30);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1A237E), 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, L_TITLE_BAMBU);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 4, 0);

    // ── 底部栏（创建一次，持久存在） ──
    lv_obj_t *footer = lv_obj_create(s_scr);
    lv_obj_set_pos(footer, 0, 290);
    lv_obj_set_size(footer, 240, 30);
    lv_obj_set_style_bg_color(footer, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_radius(footer, 0, 0);
    lv_obj_set_style_border_width(footer, 0, 0);
    lv_obj_set_style_pad_all(footer, 4, 0);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nav = lv_label_create(footer);
    lv_label_set_text(nav, L_NAV_HINT);
    lv_obj_set_style_text_font(nav, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(nav, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(nav, LV_ALIGN_LEFT_MID, 4, 0);

    s_page_ind_lbl = lv_label_create(footer);
    lv_label_set_text(s_page_ind_lbl, "1/1");
    lv_obj_set_style_text_font(s_page_ind_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_page_ind_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_page_ind_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    // ── 内容区域容器（翻页时只清理这里） ──
    s_content_area = lv_obj_create(s_scr);
    lv_obj_set_pos(s_content_area, 0, 0);
    lv_obj_set_size(s_content_area, 240, 320);
    lv_obj_set_style_bg_opa(s_content_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_content_area, 0, 0);
    lv_obj_set_style_pad_all(s_content_area, 0, 0);
    lv_obj_remove_flag(s_content_area, LV_OBJ_FLAG_SCROLLABLE);

    // 首次构建内容（标题栏/底部栏已就绪）
    STYLE_BUILD();

    // 更新页码
    {
        char pg[16];
        snprintf(pg, sizeof(pg), "%d/%d", STYLE_CUR_PAGE() + 1, STYLE_PAGE_COUNT());
        if (s_page_ind_lbl) lv_label_set_text(s_page_ind_lbl, pg);
    }

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
    s_page_ind_lbl  = NULL;
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
