// main/bambu/bambu_state.h —— 拓竹打印机状态数据结构
#pragma once

#include <stdint.h>
#include <stdbool.h>

// 打印状态枚举（来自 gcode_state 字段）
typedef enum {
    BAMBU_STATE_IDLE = 0,
    BAMBU_STATE_RUNNING,
    BAMBU_STATE_PAUSE,
    BAMBU_STATE_FINISH,
    BAMBU_STATE_FAILED,
    BAMBU_STATE_PREPARE,   // 准备中（热床/喷嘴加热）
    BAMBU_STATE_UNKNOWN,
} bambu_print_state_t;

// AMS 单个托盘信息
typedef struct {
    char   color[9];       // RRGGBB or RRGGBBAA, including terminator
    char   type[16];       // PLA/PETG/PA-CF...
    float  remain;         // percentage; -1 = unknown
    int    tag;            // 托盘标签号
    bool   active;         // 是否在使用
    bool   translucent;    // 透明/透光耗材 (拓竹用 00000000 色号+sub_brands 含 Translucent)
    bool   reported;
    char   sub_brand[32];
} bambu_ams_tray_t;

#define BAMBU_AMS_TRAY_COUNT 4
#define BAMBU_AMS_COUNT      1

// Bounded storage, independent of sparse wire IDs (e.g. HT IDs 128+).
#define BAMBU_MAX_AMS_UNITS 12
#define BAMBU_DATA_STALE_MS 60000U
typedef enum { BAMBU_AMS_UNKNOWN, BAMBU_AMS_STANDARD, BAMBU_AMS_LITE,
               BAMBU_AMS_PRO, BAMBU_AMS_HT } bambu_ams_kind_t;
typedef struct {
    int id;
    bambu_ams_kind_t kind;
    int slot_count;
    float temperature;      // -1 = not reported
    int humidity;           // actual %RH, -1 = unknown
    int humidity_level;     // legacy categorical value, never shown as %RH
    int dry_minutes;        // -1 = not reported
    int dry_state;          // protocol status, -1 = unknown
    bambu_ams_tray_t slots[BAMBU_AMS_TRAY_COUNT];
} bambu_ams_unit_t;

enum {
    BAMBU_VALID_NOZZLE = 1U << 0, BAMBU_VALID_NOZZLE_TARGET = 1U << 1,
    BAMBU_VALID_BED = 1U << 2, BAMBU_VALID_BED_TARGET = 1U << 3,
    BAMBU_VALID_CHAMBER = 1U << 4, BAMBU_VALID_LAYER = 1U << 5,
    BAMBU_VALID_TOTAL_LAYER = 1U << 6, BAMBU_VALID_PROGRESS = 1U << 7,
    BAMBU_VALID_REMAIN = 1U << 8, BAMBU_VALID_SPEED = 1U << 9,
    BAMBU_VALID_COOLING = 1U << 10, BAMBU_VALID_AUX_FAN = 1U << 11,
    BAMBU_VALID_CHAMBER_FAN = 1U << 12,
    BAMBU_VALID_SPEED_LEVEL = 1U << 13,
};

// 打印机完整状态（所有字段都来自 MQTT report JSON）
typedef struct {
    // 温度
    char printer_model[24]; // Recognized MQTT model; empty when unavailable.
    float nozzle_temp;
    float nozzle_target;
    float bed_temp;
    float bed_target;
    float chamber_temp;     // X1 顶层 chamber_temper; X2D/H2D 在 device.ctc.info.temp; P1/A1 无传感器恒 0

    // 进度
    int   layer_num;
    int   total_layer;
    int   mc_percent;       // 打印进度 0-100
    int   mc_remaining;     // 剩余时间（分钟）

    // 状态
    bambu_print_state_t state;
    char  gcode_state[16];  // 原始状态字符串

    // 速度
    int   spd_lvl;          // 速度等级 1-5
    int   spd_mag;          // 速度百分比

    // 风扇
    int   cooling_fan;      // 冷却风扇 0-100
    int   big_fan1;         // 腔体风扇1 0-100
    int   big_fan2;         // 腔体风扇2 0-100

    // AMS
    int   ams_count;
    bambu_ams_tray_t trays[BAMBU_AMS_COUNT * BAMBU_AMS_TRAY_COUNT];
    bambu_ams_tray_t vt_tray;     // 外挂料槽 (Ext, 虚拟托盘, print.vt_tray)
    int   active_tray;      // 当前使用的托盘编号
    bambu_ams_unit_t units[BAMBU_MAX_AMS_UNITS];
    int unit_count;
    int active_unit_id;     // -1 = none/external/unknown
    int active_slot_id;
    bool external_present;
    bool ams_overflow;
    uint32_t valid_mask;

    // 文件
    char  gcode_file[64];   // 文件名

    // 时间戳
    uint32_t last_update_ms; // 最后一次数据更新时间
    bool   data_valid;       // 是否有有效数据
} bambu_state_t;

// 全局状态实例（由 mqtt 模块更新，ui 模块读取）
extern bambu_state_t g_bambu_state;

// 工具函数
const char *bambu_state_str(bambu_print_state_t s);
void bambu_state_init(void);
void bambu_state_begin_connection(void);
void bambu_state_reset(bambu_state_t *st);
void bambu_state_copy(bambu_state_t *out);
void bambu_state_commit(const bambu_state_t *state);
void bambu_state_project(bambu_state_t *state, int unit_index);
bool bambu_state_fresh(const bambu_state_t *state, uint32_t now_ms);
const char *bambu_ams_kind_name(bambu_ams_kind_t kind);
