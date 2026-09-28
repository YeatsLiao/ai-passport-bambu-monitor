// main/ui/ui_lang.h —— 中英文字符串宏 + 字体宏（编译期切换）
//
// 通过 config.h 的 CFG_LANG 控制:
//   CFG_LANG = LANG_EN  → 英文, 文字用 Montserrat
//   CFG_LANG = LANG_CN  → 中文, 文字用裁剪版 Noto Sans SC (tools/gen_cn_font.js 生成)
//
// 字体宏使用规则:
//   L_FONT_TEXT / L_FONT_TEXT_BIG → 含本地化文字的标签 (随语言切换字体)
//   L_FONT_NUM*                  → 纯数字/百分比/时间标签 (始终 Montserrat, 有 28/48 大字号)
//   L_FONT_SYMBOL*               → LV_SYMBOL_* 图标标签 (符号字形只在 Montserrat 内)
#pragma once

#include "../config.h"
#include "lvgl.h"
#include "fonts/lv_font_cn.h"

#if CFG_LANG == LANG_CN
    // 中文（裁剪版 Noto Sans SC）
    #define L_TITLE_BAMBU       "Bambu"
    #define L_CONNECTED         "已连接"
    #define L_CONNECTING        "连接中..."
    #define L_DISCONNECTED      "已断开"
    #define L_NOZZLE            "喷嘴"
    #define L_BED               "热床"
    #define L_CHAMBER           "腔体"
    #define L_LAYER             "层数"
    #define L_REMAIN            "剩余"
    #define L_PROGRESS          "进度"
    #define L_SPEED             "速度"
    #define L_STATE             "状态"
    #define L_STATE_IDLE        "空闲"
    #define L_STATE_RUNNING     "打印中"
    #define L_STATE_PAUSE       "已暂停"
    #define L_STATE_FINISH      "已完成"
    #define L_STATE_FAILED      "失败"
    #define L_STATE_PREPARE     "准备中"
    #define L_AMS               "AMS"
    #define L_EXT               "外置"
    #define L_FANS              "风扇"
    #define L_COOLING           "冷却"
    #define L_PART_FAN          "模型风扇"
    #define L_EMPTY             "(空)"
    #define L_NAV_HINT          LV_SYMBOL_UP LV_SYMBOL_DOWN " 翻页   " LV_SYMBOL_REFRESH " 刷新"
    #define L_MIN               "分"
    #define L_HOUR              "时"
    // 连接失败指引 (MQTT 启动超时时叠加在监控页上, 连接成功后自动撤除)
    #define L_SETUP_TITLE       "无法连接网络"
    #define L_SETUP_HINT        "无法连接 WiFi 或打印机。长按 OK 键进入配网模式：手机连接设备热点，在网页中填写 WiFi 与打印机信息即可，无需重新编译。"
    // 配网模式 (SoftAP 热点 + 网页表单)
    #define L_PROV_TITLE        "配网模式"
    #define L_PROV_STEP1        "手机连接热点"
    #define L_PROV_STEP2        "浏览器打开"
    #define L_PROV_URL          "http://192.168.4.1"
    #define L_PROV_STEP3        "填写 WiFi 与打印机信息，保存后设备自动重启"

    // 中文字体: 裁剪版 Noto Sans SC (只含汉字子集, ASCII 自动 fallback 到 Montserrat)
    #define L_FONT_TEXT         &lv_font_cn_14
    #define L_FONT_TEXT_BIG     &lv_font_cn_20
#else
    // English (default)
    #define L_TITLE_BAMBU       "Bambu"
    #define L_CONNECTED         "Connected"
    #define L_CONNECTING        "Connecting..."
    #define L_DISCONNECTED      "Disconnected"
    #define L_NOZZLE            "Nozzle"
    #define L_BED               "Bed"
    #define L_CHAMBER           "Chamber"
    #define L_LAYER             "Layer"
    #define L_REMAIN            "Remain"
    #define L_PROGRESS          "Progress"
    #define L_SPEED             "Speed"
    #define L_STATE             "State"
    #define L_STATE_IDLE        "Idle"
    #define L_STATE_RUNNING     "Running"
    #define L_STATE_PAUSE       "Paused"
    #define L_STATE_FINISH      "Finished"
    #define L_STATE_FAILED      "Failed"
    #define L_STATE_PREPARE     "Preparing"
    #define L_AMS               "AMS"
    #define L_EXT               "Ext"
    #define L_FANS              "Fans"
    #define L_COOLING           "Cooling"
    #define L_PART_FAN          "Part Fan"
    #define L_EMPTY             "(empty)"
    #define L_NAV_HINT          LV_SYMBOL_UP LV_SYMBOL_DOWN " page   " LV_SYMBOL_REFRESH " refresh"
    #define L_MIN               "min"
    #define L_HOUR              "h"
    // Connection-failure guidance (overlay when MQTT startup times out, auto-dismissed on connect)
    #define L_SETUP_TITLE       "Network not connected"
    #define L_SETUP_HINT        "Cannot reach WiFi or the printer. Long-press OK to enter setup mode: connect your phone to the device hotspot and fill in WiFi and printer info on the web page — no rebuild needed."
    // Setup mode (SoftAP hotspot + web form)
    #define L_PROV_TITLE        "Setup Mode"
    #define L_PROV_STEP1        "Connect phone to hotspot"
    #define L_PROV_STEP2        "Open in browser"
    #define L_PROV_URL          "http://192.168.4.1"
    #define L_PROV_STEP3        "Fill in WiFi and printer info. The device reboots after saving."

    #define L_FONT_TEXT         &lv_font_montserrat_14
    #define L_FONT_TEXT_BIG     &lv_font_montserrat_20
#endif

// 纯数字标签: 两种语言都用 Montserrat（大字号只有拉丁字体有）
#define L_FONT_NUM          &lv_font_montserrat_14
#define L_FONT_NUM_MID      &lv_font_montserrat_20
#define L_FONT_NUM_BIG      &lv_font_montserrat_28
#define L_FONT_NUM_HUGE     &lv_font_montserrat_48

// LV_SYMBOL_* 图标字形只存在于 Montserrat 内, 图标标签必须用它
#define L_FONT_SYMBOL       &lv_font_montserrat_14
#define L_FONT_SYMBOL_MID   &lv_font_montserrat_20
#define L_FONT_SYMBOL_BIG   &lv_font_montserrat_28
