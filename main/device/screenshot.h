// main/device/screenshot.h —— 串口截屏协议入口
//
// 主机端工具: tools/device/capture_screen.ps1 (经 screenshot.cmd 调用)
// 协议支持: "screenshot" (@SLATE 文本协议, 带逐行校验+CRC)
//           "FAP_SCREENSHOT_V1" (官方二进制协议, RGB565LE 裸流)
#pragma once

#include <stdbool.h>

struct _lv_display_t;

// 注册 FLUSH_START 钩子并启动串口监听任务; 需在 bsp_lvgl_init() 成功后调用
bool screenshot_init(struct _lv_display_t *disp);
