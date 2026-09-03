// main/ui/ui_lang.h —— 中英文字符串宏（编译期切换）
//
// 通过 config.h 的 CFG_LANG 控制:
//   CFG_LANG = LANG_EN  → 英文
//   CFG_LANG = LANG_CN  → 中文（需中文字体支持，当前暂用英文）
#pragma once

#include "../config.h"

#if CFG_LANG == LANG_CN
    // 中文（待中文字体接入后启用）
    #define L_TITLE_BAMBU       "Bambu"
    #define L_CONNECTED         "Connected"
    #define L_CONNECTING        "Connecting..."
    #define L_DISCONNECTED      "Disconnected"
    #define L_NOZZLE            "Nozzle"
    #define L_BED               "Bed"
    #define L_CHAMBER           "Chamber"
    #define L_LAYER             "Layer"
    #define L_REMAIN            "Remain"
    #define L_SPEED             "Speed"
    #define L_STATE             "State"
    #define L_STATE_IDLE        "Idle"
    #define L_STATE_RUNNING     "Running"
    #define L_STATE_PAUSE       "Paused"
    #define L_STATE_FINISH      "Finished"
    #define L_STATE_FAILED      "Failed"
    #define L_STATE_PREPARE     "Preparing"
    #define L_AMS               "AMS"
    #define L_FANS              "Fans"
    #define L_COOLING           "Cooling"
    #define L_PART_FAN          "Part Fan"
    #define L_EMPTY             "(empty)"
    #define L_NAV_HINT          "UP/DN page  OK refresh"
    #define L_MIN               "min"
    #define L_HOUR              "h"
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
    #define L_SPEED             "Speed"
    #define L_STATE             "State"
    #define L_STATE_IDLE        "Idle"
    #define L_STATE_RUNNING     "Running"
    #define L_STATE_PAUSE       "Paused"
    #define L_STATE_FINISH      "Finished"
    #define L_STATE_FAILED      "Failed"
    #define L_STATE_PREPARE     "Preparing"
    #define L_AMS               "AMS"
    #define L_FANS              "Fans"
    #define L_COOLING           "Cooling"
    #define L_PART_FAN          "Part Fan"
    #define L_EMPTY             "(empty)"
    #define L_NAV_HINT          "UP/DN page  OK refresh"
    #define L_MIN               "min"
    #define L_HOUR              "h"
#endif
