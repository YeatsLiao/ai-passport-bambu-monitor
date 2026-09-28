// main/ui/ui_lang.c —— 双语字符串表 + 运行时语言选择
//
// 语言取自 app_config.lang (NVS > config.h 默认)。
// 中文字面量由 tools/gen_cn_font.js 扫描本文件生成裁剪字体,
// 新增/修改中文文案后需重跑: node tools/gen_cn_font.js

#include "ui_lang.h"
#include "../app_config.h"
#include "../config.h"

#define LANG_EN_IDX  0
#define LANG_CN_IDX  1

static const char *const s_strs[2][UI_STR_COUNT] = {
    [LANG_EN_IDX] = {   // English
        [UI_STR_CONNECTED]     = "Connected",
        [UI_STR_CONNECTING]    = "Connecting...",
        [UI_STR_DISCONNECTED]  = "Disconnected",
        [UI_STR_NOZZLE]        = "Nozzle",
        [UI_STR_BED]           = "Bed",
        [UI_STR_CHAMBER]       = "Chamber",
        [UI_STR_LAYER]         = "Layer",
        [UI_STR_REMAIN]        = "Remain",
        [UI_STR_PROGRESS]      = "Progress",
        [UI_STR_SPEED]         = "Speed",
        [UI_STR_STATE]         = "State",
        [UI_STR_STATE_IDLE]    = "Idle",
        [UI_STR_STATE_RUNNING] = "Running",
        [UI_STR_STATE_PAUSE]   = "Paused",
        [UI_STR_STATE_FINISH]  = "Finished",
        [UI_STR_STATE_FAILED]  = "Failed",
        [UI_STR_STATE_PREPARE] = "Preparing",
        [UI_STR_AMS]           = "AMS",
        [UI_STR_EXT]           = "Ext",
        [UI_STR_FANS]          = "Fans",
        [UI_STR_COOLING]       = "Cooling",
        [UI_STR_PART_FAN]      = "Part Fan",
        [UI_STR_EMPTY]         = "(empty)",
        [UI_STR_NAV_HINT]      = LV_SYMBOL_UP LV_SYMBOL_DOWN " page   " LV_SYMBOL_REFRESH " refresh",
        [UI_STR_MIN]           = "min",
        [UI_STR_HOUR]          = "h",
        [UI_STR_SETUP_TITLE]   = "Network not connected",
        [UI_STR_SETUP_HINT]    = "Cannot reach WiFi or the printer. Long-press OK to enter setup mode: connect your phone to the device hotspot and fill in WiFi and printer info on the web page — no rebuild needed.",
        [UI_STR_PROV_TITLE]    = "Setup Mode",
        [UI_STR_PROV_STEP1]    = "Connect phone to hotspot",
        [UI_STR_PROV_STEP2]    = "Open in browser",
        [UI_STR_PROV_STEP3]    = "Fill in WiFi and printer info. The device reboots after saving.",
    },
    [LANG_CN_IDX] = {   // 中文 (裁剪版 Noto Sans SC)
        [UI_STR_CONNECTED]     = "已连接",
        [UI_STR_CONNECTING]    = "连接中...",
        [UI_STR_DISCONNECTED]  = "已断开",
        [UI_STR_NOZZLE]        = "喷嘴",
        [UI_STR_BED]           = "热床",
        [UI_STR_CHAMBER]       = "腔体",
        [UI_STR_LAYER]         = "层数",
        [UI_STR_REMAIN]        = "剩余",
        [UI_STR_PROGRESS]      = "进度",
        [UI_STR_SPEED]         = "速度",
        [UI_STR_STATE]         = "状态",
        [UI_STR_STATE_IDLE]    = "空闲",
        [UI_STR_STATE_RUNNING] = "打印中",
        [UI_STR_STATE_PAUSE]   = "已暂停",
        [UI_STR_STATE_FINISH]  = "已完成",
        [UI_STR_STATE_FAILED]  = "失败",
        [UI_STR_STATE_PREPARE] = "准备中",
        [UI_STR_AMS]           = "AMS",
        [UI_STR_EXT]           = "外置",
        [UI_STR_FANS]          = "风扇",
        [UI_STR_COOLING]       = "冷却",
        [UI_STR_PART_FAN]      = "模型风扇",
        [UI_STR_EMPTY]         = "(空)",
        [UI_STR_NAV_HINT]      = LV_SYMBOL_UP LV_SYMBOL_DOWN " 翻页   " LV_SYMBOL_REFRESH " 刷新",
        [UI_STR_MIN]           = "分",
        [UI_STR_HOUR]          = "时",
        [UI_STR_SETUP_TITLE]   = "无法连接网络",
        [UI_STR_SETUP_HINT]    = "无法连接 WiFi 或打印机。长按 OK 键进入配网模式：手机连接设备热点，在网页中填写 WiFi 与打印机信息即可，无需重新编译。",
        [UI_STR_PROV_TITLE]    = "配网模式",
        [UI_STR_PROV_STEP1]    = "手机连接热点",
        [UI_STR_PROV_STEP2]    = "浏览器打开",
        [UI_STR_PROV_STEP3]    = "填写 WiFi 与打印机信息，保存后设备自动重启",
    },
};

static int lang_idx(void) {
    return app_config_get()->lang == LANG_CN ? LANG_CN_IDX : LANG_EN_IDX;
}

const char *ui_lang_str(ui_str_id_t id) {
    if (id >= UI_STR_COUNT) return "";
    const char *s = s_strs[lang_idx()][id];
    return s ? s : "";
}

const lv_font_t *ui_lang_font_text(void) {
    return lang_idx() == LANG_CN_IDX ? &lv_font_cn_14 : &lv_font_montserrat_14;
}

const lv_font_t *ui_lang_font_big(void) {
    return lang_idx() == LANG_CN_IDX ? &lv_font_cn_20 : &lv_font_montserrat_20;
}
