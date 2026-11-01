// main/ui/ui_settings.h —— 设备设置页 (全屏覆盖层, 独立于 12 套风格)
#pragma once

#include "bsp_button.h"
#include <stdbool.h>

// 打开设置页 (在监控最后一页按↓由 ui_monitor_key 调用; 重复调用无副作用)
void ui_settings_open(void);

// 设置页是否处于打开状态
bool ui_settings_is_open(void);

// 设置页按键处理 (ui_monitor_key 路转; 需持 LVGL 锁)
//   CLICK: UP/DOWN=选择行, OK=进入编辑/确认/执行动作
//   编辑中: UP/DOWN=调值, OK=保存, LONG=取消
//   LONG (非编辑): 返回监控页
void ui_settings_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// 注册"重置网络"动作回调 (main.c 注入 enter_prov_mode; NULL=清除)
void ui_settings_set_prov_cb(void (*cb)(void));
