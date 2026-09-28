// main/ui/ui_lang.h —— 中英文字符串 + 字体: 运行时切换
//
// 语言不再依赖编译期 CFG_LANG: 配网页选择 → NVS 持久化 (app_config.lang)
// → 重启后本模块按运行时值返回对应文案与字体。
// config.h 的 CFG_LANG 仅作为 NVS 无记录时的默认值。
//
// 宏使用规则 (与旧版一致, 调用方无感):
//   L_FONT_TEXT / L_FONT_TEXT_BIG → 含本地化文字的标签 (随语言切换字体)
//   L_FONT_NUM*                   → 纯数字/百分比/时间标签 (始终 Montserrat)
//   L_FONT_SYMBOL*                → LV_SYMBOL_* 图标标签 (符号字形只在 Montserrat 内)
//
// ⚠ L_* 现在展开为函数调用, 不能再与相邻字符串字面量做编译期拼接;
//   需要拼接处改用 "%s %s" 双参数形式。
#pragma once

#include "lvgl.h"
#include "fonts/lv_font_cn.h"

typedef enum {
    UI_STR_CONNECTED,
    UI_STR_CONNECTING,
    UI_STR_DISCONNECTED,
    UI_STR_NOZZLE,
    UI_STR_BED,
    UI_STR_CHAMBER,
    UI_STR_LAYER,
    UI_STR_REMAIN,
    UI_STR_PROGRESS,
    UI_STR_SPEED,
    UI_STR_STATE,
    UI_STR_STATE_IDLE,
    UI_STR_STATE_RUNNING,
    UI_STR_STATE_PAUSE,
    UI_STR_STATE_FINISH,
    UI_STR_STATE_FAILED,
    UI_STR_STATE_PREPARE,
    UI_STR_AMS,
    UI_STR_EXT,
    UI_STR_FANS,
    UI_STR_COOLING,
    UI_STR_PART_FAN,
    UI_STR_EMPTY,
    UI_STR_NAV_HINT,
    UI_STR_MIN,
    UI_STR_HOUR,
    UI_STR_SETUP_TITLE,
    UI_STR_SETUP_HINT,
    UI_STR_PROV_TITLE,
    UI_STR_PROV_STEP1,
    UI_STR_PROV_STEP2,
    UI_STR_PROV_STEP3,
    UI_STR_COUNT
} ui_str_id_t;

// 运行时取值 (app_config_init 之后有效)
const char *ui_lang_str(ui_str_id_t id);
const lv_font_t *ui_lang_font_text(void);   // 14px 正文
const lv_font_t *ui_lang_font_big(void);    // 20px 标题

// 两种语言一致的固定文案 (纯 ASCII, 无需进表)
#define L_TITLE_BAMBU       "Bambu"
#define L_PROV_URL          "http://192.168.4.1"

// 本地化字符串宏 (展开为函数调用, 调用点与旧版写法一致)
#define L_CONNECTED         ui_lang_str(UI_STR_CONNECTED)
#define L_CONNECTING        ui_lang_str(UI_STR_CONNECTING)
#define L_DISCONNECTED      ui_lang_str(UI_STR_DISCONNECTED)
#define L_NOZZLE            ui_lang_str(UI_STR_NOZZLE)
#define L_BED               ui_lang_str(UI_STR_BED)
#define L_CHAMBER           ui_lang_str(UI_STR_CHAMBER)
#define L_LAYER             ui_lang_str(UI_STR_LAYER)
#define L_REMAIN            ui_lang_str(UI_STR_REMAIN)
#define L_PROGRESS          ui_lang_str(UI_STR_PROGRESS)
#define L_SPEED             ui_lang_str(UI_STR_SPEED)
#define L_STATE             ui_lang_str(UI_STR_STATE)
#define L_STATE_IDLE        ui_lang_str(UI_STR_STATE_IDLE)
#define L_STATE_RUNNING     ui_lang_str(UI_STR_STATE_RUNNING)
#define L_STATE_PAUSE       ui_lang_str(UI_STR_STATE_PAUSE)
#define L_STATE_FINISH      ui_lang_str(UI_STR_STATE_FINISH)
#define L_STATE_FAILED      ui_lang_str(UI_STR_STATE_FAILED)
#define L_STATE_PREPARE     ui_lang_str(UI_STR_STATE_PREPARE)
#define L_AMS               ui_lang_str(UI_STR_AMS)
#define L_EXT               ui_lang_str(UI_STR_EXT)
#define L_FANS              ui_lang_str(UI_STR_FANS)
#define L_COOLING           ui_lang_str(UI_STR_COOLING)
#define L_PART_FAN          ui_lang_str(UI_STR_PART_FAN)
#define L_EMPTY             ui_lang_str(UI_STR_EMPTY)
#define L_NAV_HINT          ui_lang_str(UI_STR_NAV_HINT)
#define L_MIN               ui_lang_str(UI_STR_MIN)
#define L_HOUR              ui_lang_str(UI_STR_HOUR)
#define L_SETUP_TITLE       ui_lang_str(UI_STR_SETUP_TITLE)
#define L_SETUP_HINT        ui_lang_str(UI_STR_SETUP_HINT)
#define L_PROV_TITLE        ui_lang_str(UI_STR_PROV_TITLE)
#define L_PROV_STEP1        ui_lang_str(UI_STR_PROV_STEP1)
#define L_PROV_STEP2        ui_lang_str(UI_STR_PROV_STEP2)
#define L_PROV_STEP3        ui_lang_str(UI_STR_PROV_STEP3)

// 随语言切换的字体宏 (英文构建下即 Montserrat, 与旧行为一致)
#define L_FONT_TEXT         ui_lang_font_text()
#define L_FONT_TEXT_BIG     ui_lang_font_big()

// 纯数字标签: 两种语言都用 Montserrat（大字号只有拉丁字体有）
#define L_FONT_NUM          &lv_font_montserrat_14
#define L_FONT_NUM_MID      &lv_font_montserrat_20
#define L_FONT_NUM_BIG      &lv_font_montserrat_28
#define L_FONT_NUM_HUGE     &lv_font_montserrat_48

// LV_SYMBOL_* 图标字形只存在于 Montserrat 内, 图标标签必须用它
#define L_FONT_SYMBOL       &lv_font_montserrat_14
#define L_FONT_SYMBOL_MID   &lv_font_montserrat_20
#define L_FONT_SYMBOL_BIG   &lv_font_montserrat_28
