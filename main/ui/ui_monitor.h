// main/ui/ui_monitor.h —— 监控页面框架 + 风格接口声明
#pragma once

#include "bsp_button.h"
#include "config.h"
#include "lvgl.h"
#include "bambu_state.h"
#include <stddef.h>

// 屏幕对象（由 ui_monitor_enter 创建，风格文件共享）
extern lv_obj_t *s_scr;

// 内容区域容器（翻页时只清理这里，标题栏/底部栏保持不变）
extern lv_obj_t *s_content_area;

// 公共接口
void ui_monitor_enter(void);
void ui_monitor_exit(void);
void ui_monitor_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// MQTT 启动超时时调用: 叠加全屏配置指引卡 (连接成功后由刷新定时器自动撤除)
void ui_monitor_show_setup_hint(void);

// 进入配网模式后调用: 叠加全屏配网指引卡 (热点名 + 网页地址, 不可撤除)
void ui_monitor_show_prov_mode(const char *ap_ssid);
void ui_monitor_hide_setup(void);

// 框架函数（风格文件调用以重建页面）
void rebuild_page(void);

// A coherent, page-projected snapshot used by all themes during one refresh.
bambu_state_t *ui_monitor_state(void);
int ui_monitor_current_page(void);
int ui_monitor_page_count(void);
void ui_monitor_bind_page_label(lv_obj_t *label);
void ui_monitor_bind_nav_label(lv_obj_t *label);
void ui_monitor_settings_chrome(const char *hint, int row, int count);
void ui_monitor_restore(void);
void ui_monitor_bind_filament_title(lv_obj_t *label);
void ui_monitor_bind_printer_title(lv_obj_t *label);
const char *ui_monitor_connection_text(void);
bool ui_monitor_data_ready(void);
int ui_monitor_printf(char *out, size_t size, uint32_t fields, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
int ui_monitor_tray_printf(char *out, size_t size, const bambu_ams_tray_t *tray, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

// ---------------------------------------------------------------------------
// 风格接口声明（每个风格文件实现以下函数, 全部编译, 运行时由注册表选择）
// ---------------------------------------------------------------------------
// 风格函数接口: 每个风格一组, 顺序与 STYLE_* 编号 (1-12) 对应
#define UI_STYLE_OPS(name) \
    void style_##name##_build(void); \
    void style_##name##_update(void); \
    int  style_##name##_page_count(void); \
    int  style_##name##_current_page(void); \
    void style_##name##_next_page(void); \
    void style_##name##_prev_page(void);

UI_STYLE_OPS(bambu)
UI_STYLE_OPS(cyber)
UI_STYLE_OPS(sheikah)
UI_STYLE_OPS(white)
UI_STYLE_OPS(industrial)
UI_STYLE_OPS(neon)
UI_STYLE_OPS(pixel)
UI_STYLE_OPS(ssd)
UI_STYLE_OPS(f1)
UI_STYLE_OPS(gauge)
UI_STYLE_OPS(geist)
UI_STYLE_OPS(apple)
