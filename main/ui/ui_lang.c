// main/ui/ui_lang.c —— 双语字符串表 + 运行时语言选择
//
// 语言取自 app_config.lang (NVS > config.h 默认)。
// 中文字面量由 tools/font/gen_cn_font.js 扫描本文件生成裁剪字体,
// 新增/修改中文文案后需重跑: node tools/font/gen_cn_font.js

#include "ui_lang.h"
#include "app_config.h"
#include "config.h"

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
        [UI_STR_CHAMBER_FAN]   = "Chamber Fan",
        [UI_STR_EMPTY]         = "(empty)",
        [UI_STR_NAV_HINT]      = LV_SYMBOL_UP LV_SYMBOL_DOWN " page   " LV_SYMBOL_REFRESH " refresh",
        [UI_STR_MIN]           = "min",
        [UI_STR_HOUR]          = "h",
        [UI_STR_SETUP_TITLE]   = "Network not connected",
        [UI_STR_SETUP_HINT]    = "Cannot reach WiFi or the printer. Press DOWN past the last page to open Settings, then choose Network: connect your phone to the device hotspot and fill in WiFi and printer info on the web page.",
        [UI_STR_PROV_TITLE]    = "Setup Mode",
        [UI_STR_PROV_STEP1]    = "Connect phone to hotspot",
        [UI_STR_PROV_STEP2]    = "Open in browser",
        [UI_STR_PROV_STEP3]    = "Save to reboot. Hold OK to cancel setup.",
        [UI_STR_SETTINGS]      = "Settings",
        [UI_STR_BRIGHTNESS]    = "Brightness",
        [UI_STR_SLEEP]         = "Sleep After",
        [UI_STR_RESET_NET]     = "Network",
        [UI_STR_BACK]          = "Back",
        [UI_STR_OFF]           = "Off",
        [UI_STR_CONFIRM_RESET] = "Press OK again for setup",
        [UI_STR_SETTINGS_HINT] = "OK Edit  " LV_SYMBOL_UP LV_SYMBOL_DOWN " Select",
        [UI_STR_SETTINGS_EDIT_HINT] = "OK Save  Hold: cancel",
        [UI_STR_SETTINGS_CONFIRM_HINT] = "OK Setup  Hold: cancel",
        [UI_STR_SETTINGS_EDITING] = "Editing - hold to cancel",
        [UI_STR_SETTINGS_SAVED] = "Saved",
        [UI_STR_SETTINGS_SAVE_FAILED] = "Save failed. OK retries",
        [UI_STR_SETTINGS_CANCELLED] = "Cancelled",
        [UI_STR_DISPLAY_OPTIONS] = "Display & network",
        [UI_STR_WAIT_DATA]     = "Waiting for printer data",
        [UI_STR_STALE_DATA]    = "Printer data is stale",
        [UI_STR_TEMPERATURE]   = "Temperature",
        [UI_STR_HUMIDITY]      = "Humidity",
        [UI_STR_DRYING]        = "Drying left",
        [UI_STR_DEVICE_LIMIT]  = "AMS capacity exceeded",
        [UI_STR_DRY_OFF]       = "Drying off",
        [UI_STR_DRY_CHECKING]  = "Checking",
        [UI_STR_DRY_ACTIVE]    = "Drying",
        [UI_STR_DRY_STOPPING]  = "Stopping",
        [UI_STR_DRY_ERROR]     = "Drying error",
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
        [UI_STR_CHAMBER_FAN]   = "腔体风扇",
        [UI_STR_EMPTY]         = "(空)",
        [UI_STR_NAV_HINT]      = LV_SYMBOL_UP LV_SYMBOL_DOWN " 翻页   " LV_SYMBOL_REFRESH " 刷新",
        [UI_STR_MIN]           = "分",
        [UI_STR_HOUR]          = "时",
        [UI_STR_SETUP_TITLE]   = "无法连接网络",
        [UI_STR_SETUP_HINT]    = "无法连接 WiFi 或打印机。向下翻页到底再按一次打开设置页，选择「重新配网」：手机连接设备热点，在网页中填写 WiFi 与打印机信息即可，无需重新编译。",
        [UI_STR_PROV_TITLE]    = "配网模式",
        [UI_STR_PROV_STEP1]    = "手机连接热点",
        [UI_STR_PROV_STEP2]    = "浏览器打开",
        [UI_STR_PROV_STEP3]    = "保存重启，长按 OK 取消",
        [UI_STR_SETTINGS]      = "设置",
        [UI_STR_BRIGHTNESS]    = "亮度",
        [UI_STR_SLEEP]         = "熄屏",
        [UI_STR_RESET_NET]     = "重新配网",
        [UI_STR_BACK]          = "返回",
        [UI_STR_OFF]           = "关",
        [UI_STR_CONFIRM_RESET] = "再按 OK 进入配网",
        [UI_STR_SETTINGS_HINT] = "OK 编辑  " LV_SYMBOL_UP LV_SYMBOL_DOWN " 选择",
        [UI_STR_SETTINGS_EDIT_HINT] = "OK 保存  长按取消",
        [UI_STR_SETTINGS_CONFIRM_HINT] = "OK 配网  长按取消",
        [UI_STR_SETTINGS_EDITING] = "编辑中，长按可取消",
        [UI_STR_SETTINGS_SAVED] = "已保存",
        [UI_STR_SETTINGS_SAVE_FAILED] = "保存失败，OK 重试",
        [UI_STR_SETTINGS_CANCELLED] = "已取消",
        [UI_STR_DISPLAY_OPTIONS] = "显示与网络",
        [UI_STR_WAIT_DATA]     = "等待打印机数据",
        [UI_STR_STALE_DATA]    = "打印机数据已过期",
        [UI_STR_TEMPERATURE]   = "温度",
        [UI_STR_HUMIDITY]      = "湿度",
        [UI_STR_DRYING]        = "剩余干燥时间",
        [UI_STR_DEVICE_LIMIT]  = "AMS 数量超出上限",
        [UI_STR_DRY_OFF]       = "未干燥",
        [UI_STR_DRY_CHECKING]  = "检查中",
        [UI_STR_DRY_ACTIVE]    = "干燥中",
        [UI_STR_DRY_STOPPING]  = "停止中",
        [UI_STR_DRY_ERROR]     = "干燥异常",
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
