// main/ui/ui_monitor.h —— 监控页面框架 + 风格接口声明
#pragma once

#include "bsp_button.h"
#include "../config.h"
#include "lvgl.h"

// 屏幕对象（由 ui_monitor_enter 创建，风格文件共享）
extern lv_obj_t *s_scr;

// 内容区域容器（翻页时只清理这里，标题栏/底部栏保持不变）
extern lv_obj_t *s_content_area;

// 公共接口
void ui_monitor_enter(void);
void ui_monitor_exit(void);
void ui_monitor_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 框架函数（风格文件调用以重建页面）
void rebuild_page(void);

// ---------------------------------------------------------------------------
// 风格接口声明（每个风格文件实现以下函数）
// ---------------------------------------------------------------------------
#if CFG_UI_STYLE == STYLE_BAMBU
    void style_bambu_build(void);
    void style_bambu_update(void);
    int  style_bambu_page_count(void);
    int  style_bambu_current_page(void);
    void style_bambu_next_page(void);
    void style_bambu_prev_page(void);
#elif CFG_UI_STYLE == STYLE_CYBER
    void style_cyber_build(void);
    void style_cyber_update(void);
    int  style_cyber_page_count(void);
    int  style_cyber_current_page(void);
    void style_cyber_next_page(void);
    void style_cyber_prev_page(void);
#elif CFG_UI_STYLE == STYLE_SHEIKAH
    void style_sheikah_build(void);
    void style_sheikah_update(void);
    int  style_sheikah_page_count(void);
    int  style_sheikah_current_page(void);
    void style_sheikah_next_page(void);
    void style_sheikah_prev_page(void);
#elif CFG_UI_STYLE == STYLE_WHITE
    void style_white_build(void);
    void style_white_update(void);
    int  style_white_page_count(void);
    int  style_white_current_page(void);
    void style_white_next_page(void);
    void style_white_prev_page(void);
#elif CFG_UI_STYLE == STYLE_INDUSTRIAL
    void style_industrial_build(void);
    void style_industrial_update(void);
    int  style_industrial_page_count(void);
    int  style_industrial_current_page(void);
    void style_industrial_next_page(void);
    void style_industrial_prev_page(void);
#elif CFG_UI_STYLE == STYLE_NEON
    void style_neon_build(void);
    void style_neon_update(void);
    int  style_neon_page_count(void);
    int  style_neon_current_page(void);
    void style_neon_next_page(void);
    void style_neon_prev_page(void);
#elif CFG_UI_STYLE == STYLE_PIXEL
    void style_pixel_build(void);
    void style_pixel_update(void);
    int  style_pixel_page_count(void);
    int  style_pixel_current_page(void);
    void style_pixel_next_page(void);
    void style_pixel_prev_page(void);
#endif
