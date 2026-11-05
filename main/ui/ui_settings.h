// Device settings: shared behavior, theme traits, native monitor chrome.
#pragma once

#include "bsp_button.h"
#include <stdbool.h>

// 打开设置页 (在监控最后一页按↓由 ui_monitor_key 调用; 重复调用无副作用)
void ui_settings_open(void);

// 设置页是否处于打开状态
bool ui_settings_is_open(void);
void ui_settings_refresh(void);

// 设置页按键处理 (ui_monitor_key 路转; 需持 LVGL 锁)
//   CLICK: UP/DOWN=选择行, OK=进入编辑/确认/执行动作
//   编辑中: UP=增加, DOWN=减少, OK=保存, LONG OK=取消并恢复保存值
//   LONG OK: 确认配网时取消确认, 普通浏览时返回监控页
void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 注册"重新配网"动作回调 (main.c 注入 enter_prov_mode; NULL=清除)
void ui_settings_set_prov_cb(void (*cb)(void));
